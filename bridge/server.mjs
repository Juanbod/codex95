import http from "node:http";
import crypto from "node:crypto";
import dgram from "node:dgram";

const PORT = Number(process.env.CODEX95_PORT || 8787);
const DISCOVERY_PORT = Number(process.env.CODEX95_DISCOVERY_PORT || 8788);
const HOST = process.env.CODEX95_HOST || "0.0.0.0";
const MODEL = process.env.OPENAI_MODEL || "gpt-5.4-mini";
const MOCK = process.env.CODEX95_MOCK === "1";
const sessions = new Map();
const conversations = new Map();
const SESSION_TTL_MS = 30 * 60 * 1000;
const MAX_REQUEST_BYTES = 128 * 1024;

const tools = [
  tool("list_dir", "List files and directories relative to the project root.", {
    path: { type: "string", description: "Relative directory path, or . for project root." },
  }),
  tool("read_file", "Read a text file relative to the project root.", {
    path: { type: "string", description: "Relative file path." },
  }),
  tool("write_file", "Create or replace a text file relative to the project root.", {
    path: { type: "string", description: "Relative file path." },
    content: { type: "string", description: "Complete file contents." },
  }),
  tool("make_dir", "Create a directory relative to the project root.", {
    path: { type: "string", description: "Relative directory path." },
  }),
  tool("delete_path", "Delete a file or directory tree. Use only when the user explicitly requests deletion.", {
    path: { type: "string", description: "Path allowed by the current access mode." },
  }),
  tool("run_command", "Run a build, test, or project command inside the project root.", {
    command: { type: "string", description: "Command line compatible with Windows 95 COMMAND.COM." },
  }),
  tool("run_program", "Launch a compiled GUI or console program without waiting for it to close.", {
    command: { type: "string", description: "Program command line relative to the project root." },
  }),
  tool("system_info", "Inspect the Windows version and discover installed compilers and build tools.", {}),
];

function tool(name, description, properties) {
  return {
    type: "function",
    name,
    description,
    parameters: {
      type: "object",
      properties,
      required: Object.keys(properties),
      additionalProperties: false,
    },
    strict: true,
  };
}

function b64(text) {
  return Buffer.from(String(text), "utf8").toString("base64url");
}

function clientText(text) {
  return String(text)
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[\u201C\u201D]/g, '"')
    .replace(/[\u2013\u2014]/g, "-")
    .replace(/[^\x09\x0A\x0D\x20-\x7E]/g, "?");
}

function protocol(fields) {
  return Object.entries(fields).map(([key, value]) => `${key}=${value ?? ""}`).join("\r\n") + "\r\n";
}

function oneLine(value) {
  return String(value).replace(/[\r\n]+/g, " ");
}

function send(res, fields, status = 200) {
  const body = protocol(fields);
  res.writeHead(status, {
    "Content-Type": "text/plain; charset=us-ascii",
    "Content-Length": Buffer.byteLength(body),
    "Connection": "close",
  });
  res.end(body);
}

async function readForm(req) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > MAX_REQUEST_BYTES) throw new Error("request body is too large");
    chunks.push(chunk);
  }
  return Object.fromEntries(new URLSearchParams(Buffer.concat(chunks).toString("utf8")));
}

function instructions(root, profile, access) {
  const accessText = access === "full"
    ? `FULL COMPUTER ACCESS is enabled. Tool paths may be absolute and may target files outside the project root.
You may run programs and commands anywhere on the Libretto when needed for the user's task. Destructive deletion still requires an explicit user request.`
    : `PROJECT-ONLY ACCESS is enabled. All tool paths must be relative to the project root and cannot access files outside it.`;
  return `You are Codex95, a coding agent operating on a Toshiba Libretto running Windows 95.
You are reached through a bridge running on a separate modern computer. The bridge only relays this conversation and tool calls.
All provided tools execute on the target Toshiba Libretto, not on the bridge computer.
Do not assume that software available on the bridge is installed on the Libretto. Only use target tools reported in the device profile or discovered with system_info.
The user project root is ${root}.
${accessText}
The client reported this actual target device profile:
--- device profile ---
${profile || "No device profile was supplied by this older client."}
--- end device profile ---
Treat the reported OS, CPU, RAM, screen, codepages, disk space, and installed tools as authoritative constraints.
Use tools to inspect and modify the real project. Prefer small, Windows 95-compatible programs.
Commands run through COMMAND.COM. Avoid PowerShell, Unix shell syntax, Node.js, Python, and modern-only APIs.
Use system_info before the first build when the available compiler is unknown.
Use run_command for builds and short tests. Use run_program to launch completed GUI applications.
Prefer ASCII-only source code and filenames unless the user explicitly requests another encoding.
Never delete files unless the user explicitly asks. Briefly summarize completed work in plain ASCII English.`;
}

async function openaiRequest(payload) {
  const key = process.env.OPENAI_API_KEY;
  if (!key) throw new Error("OPENAI_API_KEY is not set on the bridge");
  const response = await fetch("https://api.openai.com/v1/responses", {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${key}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify(payload),
  });
  const json = await response.json();
  if (!response.ok) throw new Error(json?.error?.message || `OpenAI API returned ${response.status}`);
  return json;
}

function nextFromResponse(session, response) {
  session.responseId = response.id;
  conversations.set(session.key, response.id);
  const call = response.output?.find((item) => item.type === "function_call");
  if (call) {
    session.callId = call.call_id;
    const args = JSON.parse(call.arguments || "{}");
    const fields = { status: "action", session: session.id, action: call.name };
    if (args.path !== undefined) fields.path = oneLine(args.path);
    if (args.command !== undefined) fields.command = oneLine(args.command);
    if (args.content !== undefined) fields.data = b64(args.content);
    return fields;
  }
  const text = response.output
    ?.filter((item) => item.type === "message")
    .flatMap((item) => item.content || [])
    .filter((item) => item.type === "output_text")
    .map((item) => item.text)
    .join("\n");
  return { status: "message", session: session.id, message: b64(clientText(text || "Done.")) };
}

async function startOpenAI(session, prompt) {
  const payload = {
    model: MODEL,
    instructions: instructions(session.root, session.profile, session.access),
    input: prompt,
    tools,
    parallel_tool_calls: false,
  };
  const previous = conversations.get(session.key);
  if (previous) payload.previous_response_id = previous;
  const response = await openaiRequest(payload);
  return nextFromResponse(session, response);
}

async function continueOpenAI(session, result) {
  const response = await openaiRequest({
    model: MODEL,
    instructions: instructions(session.root, session.profile, session.access),
    previous_response_id: session.responseId,
    input: [{ type: "function_call_output", call_id: session.callId, output: result }],
    tools,
    parallel_tool_calls: false,
  });
  return nextFromResponse(session, response);
}

function startMock(session, prompt) {
  if (prompt.toLowerCase().includes("full smoke")) {
    session.mockFull = true;
    session.mockStep = 1;
    return { status: "action", session: session.id, action: "system_info" };
  }
  if (prompt.toLowerCase().includes("absolute smoke")) {
    session.mockPath = `${session.root}\\ABSOLUTE.TXT`;
  }
  if (prompt.toLowerCase().includes("delete smoke")) {
    session.mockDelete = true;
    return { status: "action", session: session.id, action: "delete_path", path: "DELETE-ME" };
  }
  session.mockStep = 1;
  return {
    status: "action",
    session: session.id,
    action: "write_file",
    path: session.mockPath || "HELLO.C",
    data: b64('#include <stdio.h>\r\n\r\nint main(void) {\r\n    puts("Hello from Codex95!");\r\n    return 0;\r\n}\r\n'),
  };
}

function continueMock(session, result) {
  if (session.mockDelete) {
    if (!result.includes("OK: deleted")) throw new Error("delete mock failed");
    return { status: "message", session: session.id, message: b64("Delete mock task complete.") };
  }
  if (session.mockFull) {
    if (session.mockStep === 1 && !result.includes("Windows"))
      throw new Error("full mock failed: system_info result missing");
    if (session.mockStep === 2 && !result.includes("OK"))
      throw new Error("full mock failed: make_dir result missing");
    if (session.mockStep === 3 && !result.includes("OK: wrote"))
      throw new Error("full mock failed: write_file result missing");
    if (session.mockStep === 4 && (!result.includes("command-ok") || !result.includes("[exit=0]")))
      throw new Error("full mock failed: run_command output missing");
    session.mockStep++;
    if (session.mockStep === 2)
      return { status: "action", session: session.id, action: "make_dir", path: "SMOKE" };
    if (session.mockStep === 3)
      return { status: "action", session: session.id, action: "write_file", path: "SMOKE\\DATA.TXT", data: b64("file-ok\r\n") };
    if (session.mockStep === 4)
      return { status: "action", session: session.id, action: "run_command", command: "echo command-ok" };
    return { status: "message", session: session.id, message: b64("Full mock task complete.") };
  }
  if (session.mockStep === 1) {
    session.mockStep = 2;
    return { status: "action", session: session.id, action: "read_file", path: session.mockPath || "HELLO.C" };
  }
  return {
    status: "message",
    session: session.id,
    message: b64("Mock task complete. Created and verified HELLO.C in the Libretto project directory."),
  };
}

const server = http.createServer(async (req, res) => {
  try {
    if (req.method === "GET" && req.url === "/health") {
      return send(res, { status: "ok", mode: MOCK ? "mock" : "openai", model: MODEL });
    }
    if (req.method === "POST" && req.url === "/session/start") {
      const form = await readForm(req);
      if (!form.prompt || !form.root) return send(res, { status: "error", message: b64("prompt and root are required") }, 400);
      const id = crypto.randomBytes(8).toString("hex");
      const profile = Buffer.from(form.profile || "", "base64url").toString("utf8");
      const access = form.access === "full" ? "full" : "project";
      const session = { id, root: form.root, profile, access, key: form.root.toLowerCase(), touchedAt: Date.now() };
      sessions.set(id, session);
      return send(res, MOCK ? startMock(session, form.prompt) : await startOpenAI(session, form.prompt));
    }
    if (req.method === "POST" && req.url === "/session/continue") {
      const form = await readForm(req);
      const session = sessions.get(form.session);
      if (!session) return send(res, { status: "error", message: b64("unknown or expired session") }, 404);
      session.touchedAt = Date.now();
      const result = Buffer.from(form.result || "", "base64url").toString("utf8");
      const reply = MOCK ? continueMock(session, result) : await continueOpenAI(session, result);
      if (reply.status === "message" || reply.status === "error") sessions.delete(session.id);
      return send(res, reply);
    }
    return send(res, { status: "error", message: b64("not found") }, 404);
  } catch (error) {
    return send(res, { status: "error", message: b64(clientText(error.message || String(error))) }, 500);
  }
});

setInterval(() => {
  const cutoff = Date.now() - SESSION_TTL_MS;
  for (const [id, session] of sessions) {
    if (session.touchedAt < cutoff) sessions.delete(id);
  }
}, 5 * 60 * 1000).unref();

server.listen(PORT, HOST, () => {
  console.log(`Codex95 bridge listening on http://${HOST}:${PORT} (${MOCK ? "mock" : MODEL})`);
});

const discovery = dgram.createSocket("udp4");
discovery.on("error", (error) => {
  console.warn(`Codex95 LAN discovery disabled: ${error.message}`);
  discovery.close();
});
discovery.on("message", (message, remote) => {
  if (message.toString("ascii").trim() === "CODEX95_DISCOVER") {
    discovery.send(Buffer.from(`CODEX95_BRIDGE ${PORT}`, "ascii"), remote.port, remote.address);
  }
});
discovery.bind(DISCOVERY_PORT, HOST, () => {
  discovery.setBroadcast(true);
  console.log(`Codex95 LAN discovery listening on UDP ${DISCOVERY_PORT}`);
});
