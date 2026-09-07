import http from "node:http";
import crypto from "node:crypto";
import dgram from "node:dgram";
import fs from "node:fs";

const PORT = Number(process.env.CODEX95_PORT || 8787);
const DISCOVERY_PORT = Number(process.env.CODEX95_DISCOVERY_PORT || 8788);
const HOST = process.env.CODEX95_HOST || "0.0.0.0";
let runtimeKey = process.env.OPENAI_API_KEY || "";
const CLIENT_VERSION = fs.readFileSync(new URL("../VERSION", import.meta.url), "ascii").trim();
const MODELS = [
  "gpt-5.6-luna",
  "gpt-5.6-terra",
  "gpt-5.6",
  "gpt-5.6-sol",
  "gpt-6-astra",
  "chat-latest",
  "gpt-5.5",
  "gpt-5.5-pro",
  "gpt-5.4-mini",
  "gpt-5.4",
  "gpt-5.4-nano",
];
const DEFAULT_MODEL = process.env.OPENAI_MODEL || MODELS[0];
const MOCK = process.env.CODEX95_MOCK === "1";
const sessions = new Map();
const STATE_PATH = new URL(".codex95-state.json", import.meta.url);
const CLIENT_DIR = new URL("../build/", import.meta.url);
const SESSION_TTL_MS = 30 * 60 * 1000;
const MAX_REQUEST_BYTES = 128 * 1024;

function loadConversations() {
  try {
    return new Map(Object.entries(JSON.parse(fs.readFileSync(STATE_PATH, "utf8"))));
  } catch {
    return new Map();
  }
}

function saveConversations() {
  try {
    fs.writeFileSync(STATE_PATH, JSON.stringify(Object.fromEntries(conversations), null, 2));
  } catch (error) {
    console.warn(`Could not save conversation state: ${error.message}`);
  }
}

const conversations = loadConversations();

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
    .replace(/[\u2013\u2014]/g, "-");
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
    "Content-Type": "text/plain; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Connection": "close",
  });
  res.end(body);
}

function sendHtml(res, body, status = 200) {
  res.writeHead(status, {
    "Content-Type": "text/html; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Connection": "close",
  });
  res.end(body);
}

function sendFile(res, fileUrl) {
  try {
    const stat = fs.statSync(fileUrl);
    res.writeHead(200, {
      "Content-Type": "application/octet-stream",
      "Content-Length": stat.size,
      "Connection": "close",
    });
    fs.createReadStream(fileUrl).pipe(res);
  } catch {
    send(res, { status: "error", message: b64("file not found") }, 404);
  }
}

function clientManifest() {
  const file = new URL("CODEX95W.EXE", CLIENT_DIR);
  const data = fs.readFileSync(file);
  return {
    status: "ok",
    version: CLIENT_VERSION,
    size: data.length,
    sha256: crypto.createHash("sha256").update(data).digest("hex"),
    models: b64(MODELS.join("\n")),
    default_model: DEFAULT_MODEL,
  };
}

function isLocalRequest(req) {
  const address = req.socket.remoteAddress || "";
  return address === "127.0.0.1" || address === "::1" || address === "::ffff:127.0.0.1";
}

function setupPage(message = "") {
  return `<!doctype html>
<html><head><meta charset="utf-8"><title>Codex95 Bridge Setup</title>
<style>
body{font:16px system-ui,sans-serif;max-width:560px;margin:48px auto;padding:0 20px;color:#202124}
input,button{font:inherit;padding:9px}input{width:100%;box-sizing:border-box;margin:8px 0 16px}
button{cursor:pointer}.status{padding:10px;background:#eef3f8;border-left:4px solid #3976a8}
small{color:#5f6368}
</style></head><body>
<h1>Codex95 Bridge</h1>
<p class="status">${message || (runtimeKey ? "API key is loaded in memory." : "API key is not configured.")}</p>
<form method="post" action="/setup">
<label>OpenAI API key</label>
<input type="password" name="key" autocomplete="off" placeholder="Paste API key">
<button type="submit">Load key into bridge memory</button>
</form>
<p><small>The key is not sent to Windows 95 and is not written to disk. It is forgotten when the bridge stops. Model selection is controlled by the Codex95 client.</small></p>
</body></html>`;
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
Never delete files unless the user explicitly asks. Reply in the user's language and keep the final summary concise.
Format chat replies for an 800x480 plain-text window: use short paragraphs and simple lists with real line breaks, and avoid tables.`;
}

async function openaiRequest(payload) {
  const key = runtimeKey;
  if (!key) throw new Error(`API key is not configured. Open http://127.0.0.1:${PORT}/setup on the bridge PC.`);
  for (let attempt = 1; attempt <= 3; attempt++) {
    const response = await fetch("https://api.openai.com/v1/responses", {
      method: "POST",
      headers: {
        "Authorization": `Bearer ${key}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify(payload),
    });
    const text = await response.text();
    let json;
    try {
      json = JSON.parse(text);
    } catch {
      if (attempt < 3 && response.status >= 500) {
        await new Promise((resolve) => setTimeout(resolve, attempt * 1000));
        continue;
      }
      throw new Error(`OpenAI API returned ${response.status}: ${text.slice(0, 240)}`);
    }
    if (!response.ok) {
      if (attempt < 3 && response.status >= 500) {
        await new Promise((resolve) => setTimeout(resolve, attempt * 1000));
        continue;
      }
      throw new Error(json?.error?.message || `OpenAI API returned ${response.status}`);
    }
    return json;
  }
  throw new Error("OpenAI API request failed after retries");
}

function nextFromResponse(session, response) {
  session.responseId = response.id;
  if (["failed", "cancelled", "incomplete"].includes(response.status)) {
    const reason = response.error?.message || response.incomplete_details?.reason || response.status;
    return { status: "error", session: session.id, message: b64(clientText(`OpenAI response ${reason}`)) };
  }
  const call = response.output?.find((item) => item.type === "function_call");
  if (call) {
    session.callId = call.call_id;
    let args;
    try {
      args = JSON.parse(call.arguments || "{}");
    } catch {
      return { status: "error", session: session.id, message: b64("OpenAI returned invalid tool arguments.") };
    }
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
  conversations.set(session.key, response.id);
  saveConversations();
  return { status: "message", session: session.id, message: b64(clientText(text || "Done.")) };
}

async function startOpenAI(session, prompt) {
  const payload = {
    model: session.model,
    instructions: instructions(session.root, session.profile, session.access),
    input: prompt,
    tools,
    parallel_tool_calls: false,
  };
  const previous = conversations.get(session.key);
  if (previous) payload.previous_response_id = previous;
  let response;
  try {
    response = await openaiRequest(payload);
  } catch (error) {
    if (!previous || !/previous|response.*(?:not found|expired|exist)/i.test(error.message)) throw error;
    conversations.delete(session.key);
    saveConversations();
    delete payload.previous_response_id;
    response = await openaiRequest(payload);
  }
  return nextFromResponse(session, response);
}

async function continueOpenAI(session, result) {
  const response = await openaiRequest({
    model: session.model,
    instructions: instructions(session.root, session.profile, session.access),
    previous_response_id: session.responseId,
    input: [{ type: "function_call_output", call_id: session.callId, output: result }],
    tools,
    parallel_tool_calls: false,
  });
  return nextFromResponse(session, response);
}

function startMock(session, prompt) {
  if (prompt.toLowerCase().includes("language smoke")) {
    return { status: "message", session: session.id, message: b64(clientText("Language smoke: Привет мир / こんにちは世界")) };
  }
  if (prompt.toLowerCase().includes("model smoke")) {
    return { status: "message", session: session.id, message: b64(`Selected model: ${session.model}`) };
  }
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
      return send(res, { status: "ok", mode: MOCK ? "mock" : "openai", model: DEFAULT_MODEL, version: CLIENT_VERSION, key: runtimeKey ? "loaded" : "missing" });
    }
    if ((req.method === "GET" || req.method === "POST") && req.url === "/client/manifest") {
      return send(res, clientManifest());
    }
    if (req.method === "GET" && req.url === "/client/CODEX95W.EXE") {
      return sendFile(res, new URL("CODEX95W.EXE", CLIENT_DIR));
    }
    if (req.method === "GET" && req.url === "/client/CODEX95.EXE") {
      return sendFile(res, new URL("CODEX95.EXE", CLIENT_DIR));
    }
    if (req.method === "GET" && req.url === "/setup") {
      if (!isLocalRequest(req)) return sendHtml(res, "<h1>Local access only</h1>", 403);
      return sendHtml(res, setupPage());
    }
    if (req.method === "POST" && req.url === "/setup") {
      if (!isLocalRequest(req)) return sendHtml(res, "<h1>Local access only</h1>", 403);
      const form = await readForm(req);
      if (!form.key) return sendHtml(res, setupPage("No key was entered."), 400);
      runtimeKey = form.key;
      return sendHtml(res, setupPage("API key loaded. Codex95 can now send real tasks."));
    }
    if (req.method === "POST" && req.url === "/session/start") {
      const form = await readForm(req);
      if (!form.prompt || !form.root) return send(res, { status: "error", message: b64("prompt and root are required") }, 400);
      const id = crypto.randomBytes(8).toString("hex");
      const profile = Buffer.from(form.profile || "", "base64url").toString("utf8");
      const access = form.access === "full" ? "full" : "project";
      const model = /^[A-Za-z0-9._-]{1,80}$/.test(form.model || "") ? form.model : DEFAULT_MODEL;
      const session = { id, root: form.root, profile, access, model, key: `${form.root.toLowerCase()}|${model}`, touchedAt: Date.now() };
      sessions.set(id, session);
      const reply = MOCK ? startMock(session, form.prompt) : await startOpenAI(session, form.prompt);
      if (reply.status === "message" || reply.status === "error") sessions.delete(id);
      return send(res, reply);
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
  console.log(`Codex95 bridge listening on http://${HOST}:${PORT} (${MOCK ? "mock" : DEFAULT_MODEL})`);
  console.log(`Local setup page: http://127.0.0.1:${PORT}/setup`);
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
