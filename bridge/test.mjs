import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import crypto from "node:crypto";
import dgram from "node:dgram";
import fs from "node:fs";

const port = 18787;
const version = fs.readFileSync(new URL("../VERSION", import.meta.url), "ascii").trim();
const guiSource = fs.readFileSync(new URL("../client/codex95_gui.c", import.meta.url), "ascii");
const consoleSource = fs.readFileSync(new URL("../client/codex95.c", import.meta.url), "ascii");
const resourceSource = fs.readFileSync(new URL("../client/CODEX95.RC", import.meta.url), "ascii");
assert.equal(guiSource.includes(`#define APP_VERSION "${version}"`), true);
assert.equal(resourceSource.includes(`VALUE "ProductVersion", "${version}\\0"`), true);
assert.equal(guiSource.includes("CP_UTF8"), false);
assert.equal(consoleSource.includes("CP_UTF8"), false);
assert.equal(guiSource.includes("CODEX95W.OLD"), true);
assert.equal(guiSource.includes("fc /b CODEX95W.NEW CODEX95W.EXE"), true);
assert.equal(guiSource.includes(":again\\r\\n"), false);
const child = spawn(process.execPath, ["server.mjs"], {
  cwd: new URL(".", import.meta.url),
  env: {
    ...process.env,
    CODEX95_MOCK: "1",
    CODEX95_PORT: String(port),
    CODEX95_DISCOVERY_PORT: String(port + 1),
    CODEX95_HOST: "127.0.0.1",
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
  const response = await fetch(`http://127.0.0.1:${port}${path}`, {
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
    socket.send(Buffer.from("CODEX95_DISCOVER"), port + 1, "127.0.0.1");
  });
}

try {
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("bridge startup timeout")), 3000);
    child.stdout.once("data", () => { clearTimeout(timer); resolve(); });
  });
  assert.equal(await discover(), `CODEX95_BRIDGE ${port}`);
  const healthResponse = await fetch(`http://127.0.0.1:${port}/health`);
  const health = parse(await healthResponse.text());
  assert.equal(health.status, "ok");
  assert.equal(health.version, version);
  assert.equal(health.model, "gpt-5.6-luna");
  const manifest = await post("/client/manifest", {});
  const models = Buffer.from(manifest.models, "base64url").toString("utf8").split("\n");
  assert.equal(manifest.version, version);
  assert.equal(manifest.default_model, "gpt-5.6-luna");
  assert.equal(models.includes("gpt-6-astra"), true);
  assert.equal(models.includes("gpt-5.6-terra"), true);
  assert.equal(models.includes("chat-latest"), true);
  for (const model of models) assert.equal(guiSource.includes(`"${model}"`), true);
  const clientResponse = await fetch(`http://127.0.0.1:${port}/client/CODEX95W.EXE`);
  const client = Buffer.from(await clientResponse.arrayBuffer());
  assert.equal(clientResponse.status, 200);
  assert.equal(client.subarray(0, 2).toString("ascii"), "MZ");
  assert.equal(Number(manifest.size), client.length);
  assert.equal(manifest.sha256, crypto.createHash("sha256").update(client).digest("hex"));
  const setup = await fetch(`http://127.0.0.1:${port}/setup`);
  assert.equal(setup.status, 200);
  assert.equal((await setup.text()).includes("API key is not configured"), true);
  const configured = await fetch(`http://127.0.0.1:${port}/setup`, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ key: "test-key-not-real" }),
  });
  assert.equal(configured.status, 200);
  assert.equal((await configured.text()).includes("API key loaded"), true);
  const profile = Buffer.from("Device: Toshiba Libretto 70CT\r\nRAM: 24 MB\r\nScreen: 800x480\r\n").toString("base64url");
  const modelCheck = await post("/session/start", {
    prompt: "model smoke",
    root: "C:\\DEV\\MODEL",
    profile,
    access: "project",
    model: "gpt-5.6-terra",
  });
  assert.equal(Buffer.from(modelCheck.message, "base64url").toString("utf8"), "Selected model: gpt-5.6-terra");
  const languageCheck = await post("/session/start", {
    prompt: "language smoke",
    root: "C:\\DEV\\LANG",
    profile,
    access: "project",
    model: "gpt-5.6-luna",
  });
  assert.equal(
    Buffer.from(languageCheck.message, "base64url").toString("utf8"),
    "Language smoke: Привет мир / こんにちは世界",
  );
  const start = await post("/session/start", { prompt: "make hello", root: "C:\\DEV\\DEMO", profile, access: "project", model: "gpt-5.6-luna" });
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
