const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

const source = fs.readFileSync(path.join(__dirname, "../src/renderer/renderer.js"), "utf8");
const healthy = {
  running: true, ad7124: true, eeprom: true, rtc: true, sd: true,
  sdTotalMb: 8192, sdFreeMb: 8191, batteryMv: 4000, errorCode: 0,
};

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
  return { promise, resolve, reject };
}

function renderer(overrides = {}) {
  const elements = new Map();
  const events = [];
  function element(selector) {
    if (!elements.has(selector)) {
      elements.set(selector, {
        textContent: "", innerHTML: "", style: {}, value: "0", dataset: {},
        classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
        addEventListener(event, callback) { this[event] = callback; },
      });
    }
    return elements.get(selector);
  }
  const plantApi = {
    async readStatus() { events.push("status"); return healthy; },
    async readChannels() {
      events.push("channels");
      return [{ ch: 0, name: "CH0", valid: true, voltageUv: 120, raw: 50 }];
    },
    async downloadFiles() { events.push("download"); return { ok: ["260709.CSV"] }; },
    async openDir() { return { files: [], status: healthy }; },
    onDownloadProgress() { return () => events.push("unsubscribe"); },
    ...overrides,
  };
  const context = vm.createContext({
    document: {
      querySelector: element,
      querySelectorAll: (selector) => selector === ".ch-check:checked" ? [{ value: "0" }] : [],
      addEventListener() {},
    },
    window: { plantApi, confirm: () => true },
    gsap: {
      defaults() {}, matchMedia: () => ({}), from() {}, fromTo() {}, killTweensOf() {}, set() {},
      to(_target, options) { options.onComplete?.(); },
    },
  });
  // Run the real renderer functions without starting the full application UI.
  vm.runInContext(source.replace(/\bboot\(\);\s*$/, "") + `
    globalThis.api = { state, setConnected, refreshStatus, refreshStatusAfterOperation,
      switchView, readChannels, downloadFiles, openDir, bindEvents };
  `, context);
  context.api.setConnected(true, { deviceId: 1, fwVersion: 0x101, cfgVersion: 3 });
  return { ...context.api, element, events };
}

test("healthy modules and a missing SD card are displayed independently", async () => {
  const r = renderer({ readStatus: async () => ({ ...healthy, sd: false, sdTotalMb: 0 }) });
  await r.refreshStatus();
  const bits = r.element("#systemBits").innerHTML;
  for (const name of ["AD7124", "EEPROM", "RTC", "RUN"]) assert.match(bits, new RegExp(`${name} 正常`));
  assert.match(bits, /SD 异常/);
});

test("returning to the dashboard replaces an earlier all-failed snapshot", async () => {
  const r = renderer();
  r.element("#systemBits").innerHTML = "AD7124 异常 EEPROM 异常 RTC 异常 SD 异常";
  r.switchView("dashboard");
  await r.refreshStatus();
  assert.equal(r.events.filter((event) => event === "status").length, 1);
  assert.match(r.element("#systemBits").innerHTML, /SD 正常/);
  assert.doesNotMatch(r.element("#systemBits").innerHTML, /异常/);
});

test("reading channels refreshes the dashboard after the channel request", async () => {
  const r = renderer();
  await r.readChannels();
  assert.deepEqual(r.events, ["channels", "status"]);
  assert.match(r.element("#channelResults").innerHTML, /CH0/);
  assert.match(r.element("#systemBits").innerHTML, /AD7124 正常/);
});

test("a status timeout does not turn a successful channel read into a failure", async () => {
  const r = renderer({ readStatus: async () => { throw new Error("timeout"); } });
  await r.readChannels();
  assert.match(r.element("#channelResults").innerHTML, /120/);
  assert.match(r.element("#toast").textContent, /读取状态失败/);
});

test("disconnecting discards a late status response", async () => {
  const pending = deferred();
  const r = renderer({ readStatus: () => pending.promise });
  const request = r.refreshStatus();
  r.setConnected(false);
  pending.resolve(healthy);
  await request;
  assert.match(r.element("#systemBits").innerHTML, /未读取/);
  assert.doesNotMatch(r.element("#systemBits").innerHTML, /正常/);
});

test("concurrent refreshes share a single request", async () => {
  const pending = deferred();
  let count = 0;
  const r = renderer({ readStatus: () => { count++; return pending.promise; } });
  const first = r.refreshStatus();
  const second = r.refreshStatus();
  assert.equal(count, 1);
  pending.resolve(healthy);
  await Promise.all([first, second]);
});

test("YMODEM waits for status, blocks new refreshes, then refreshes after completion", async () => {
  const pendingStatus = deferred();
  const pendingDownload = deferred();
  const calls = [];
  let count = 0;
  const r = renderer({
    readStatus: () => { calls.push("status"); return ++count === 1 ? pendingStatus.promise : healthy; },
    downloadFiles: () => { calls.push("download"); return pendingDownload.promise; },
  });
  r.state.files = [{ row: 0, name: "260709.CSV", size: 200 }];
  r.state.selectedFiles.add(0);
  const status = r.refreshStatus();
  const download = r.downloadFiles();
  await r.refreshStatus();
  assert.deepEqual(calls, ["status"]);
  pendingStatus.resolve(healthy);
  await status;
  await new Promise(setImmediate);
  assert.deepEqual(calls, ["status", "download"]);
  await r.refreshStatus();
  await r.downloadFiles(); // A second click must not start another transfer.
  assert.deepEqual(calls, ["status", "download"]);
  pendingDownload.resolve({ ok: ["260709.CSV"] });
  await download;
  assert.equal(r.state.downloading, false);
  assert.deepEqual(calls, ["status", "download", "status"]);
});

test("canceling the download dialog releases the status refresh guard", async () => {
  const r = renderer({ downloadFiles: async () => ({ canceled: true }) });
  r.state.files = [{ row: 0, name: "260709.CSV", size: 200 }];
  r.state.selectedFiles.add(0);
  await r.downloadFiles();
  assert.equal(r.state.downloading, false);
  assert.ok(r.events.includes("status"));
});

test("directory results update the dashboard and the manual refresh button reads status", async () => {
  const r = renderer();
  await r.openDir();
  assert.match(r.element("#systemBits").innerHTML, /SD 正常/);
  r.bindEvents();
  r.element("#refreshStatus").click();
  await r.refreshStatus();
  assert.equal(r.events.filter((event) => event === "status").length, 1);
});
