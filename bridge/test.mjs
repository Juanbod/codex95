import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import dgram from "node:dgram";

const port = 18787;
const child = spawn(process.execPath, ["server.mjs"], {
  cwd: new URL(".", import.meta.url),
  env: {
    ...process.env,
    CODEX95_MOCK: "1",
    CODEX95_PORT: String(port),
    CODEX95_DISCOVERY_PORT: String(port + 1),
    CODEX95_HOST: "localhost",
  },
  stdio: ["ignore", "pipe", "inherit"],
});

function parse(text) {
  return Object.fromEntries(text.trim().split(/\r?\n/).map((line) => {
    const i = line.indexOf("=");
    return [line.slice(0, i), line.slice(i + 1)];
  }));
}

async function post(path, form) {
  const response = await fetch(`http://localhost:${port}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(form),
  });
  return parse(await response.text());
}

async function discover() {
  const socket = dgram.createSocket("udp4");
  return await new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      socket.close();
      reject(new Error("discovery timeout"));
    }, 2000);
    socket.once("message", (message) => {
      clearTimeout(timer);
      socket.close();
      resolve(message.toString("ascii"));
    });
    socket.send(Buffer.from("CODEX95_DISCOVER"), port + 1, "localhost");
  });
}

try {
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("bridge startup timeout")), 3000);
    child.stdout.once("data", () => { clearTimeout(timer); resolve(); });
  });
  assert.equal(await discover(), `CODEX95_BRIDGE ${port}`);
  const profile = Buffer.from("Device: Toshiba Libretto 70CT\r\nRAM: 24 MB\r\nScreen: 800x480\r\n").toString("base64url");
  const start = await post("/session/start", { prompt: "make hello", root: "C:\\DEV\\DEMO", profile, access: "project" });
  assert.equal(start.status, "action");
  assert.equal(start.action, "write_file");
  assert.equal(Buffer.from(start.data, "base64url").toString("utf8").includes("Hello from Codex95"), true);

  const next = await post("/session/continue", {
    session: start.session,
    result: Buffer.from("OK: wrote 100 bytes").toString("base64url"),
  });
  assert.equal(next.action, "read_file");

  const done = await post("/session/continue", {
    session: start.session,
    result: Buffer.from("file contents").toString("base64url"),
  });
  assert.equal(done.status, "message");
  console.log("bridge protocol test passed");
} finally {
  child.kill();
}
