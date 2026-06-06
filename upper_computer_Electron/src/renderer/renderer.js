const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

const GAIN_VALUES = [1, 2, 4, 8, 16, 32, 64, 128];
const LARGE_FILE_BYTES = 1024 * 1024;
const CONTROL_FORCE_LABELS = ["AUTO", "ON", "OFF"];
const CONFIG_TIME_MAX_SEC = 86400;
const CONFIG_I32_LIMIT = 2000000000;

const state = {
  connected: false,
  files: [],
  selectedFiles: new Set(),
  config: null,
};

const titles = {
  dashboard: "总览",
  clock: "时钟",
  channels: "通道",
  config: "配置",
  files: "文件",
};

gsap.defaults({ duration: 0.42, ease: "power2.out" });
const mm = gsap.matchMedia();

function animateIntro() {
  mm.add({
    reduceMotion: "(prefers-reduced-motion: reduce)",
  }, (ctx) => {
    if (ctx.conditions.reduceMotion) return;
    const tl = gsap.timeline({ defaults: { ease: "power3.out" } });
    tl.from(".rail", { x: -42, autoAlpha: 0, duration: 0.55 })
      .from(".topbar", { y: -24, autoAlpha: 0, duration: 0.45 }, "<0.12")
      .from(".metric-card", { y: 24, autoAlpha: 0, stagger: 0.06 }, "<0.1")
      .from(".band", { y: 20, autoAlpha: 0, stagger: 0.05 }, "<0.1");
  });
}

function switchView(name) {
  const current = $(".view.active");
  const next = $(`#${name}`);
  if (!next || current === next) return;
  $(".nav-item.active")?.classList.remove("active");
  $(`.nav-item[data-view="${name}"]`)?.classList.add("active");
  $("#viewTitle").textContent = titles[name] || name;
  gsap.to(current, {
    y: 10,
    autoAlpha: 0,
    duration: 0.18,
    onComplete: () => {
      current.classList.remove("active");
      current.style.opacity = "";
      current.style.visibility = "";
      next.classList.add("active");
      gsap.fromTo(next, { y: 16, autoAlpha: 0 }, { y: 0, autoAlpha: 1, duration: 0.34 });
    },
  });
}

function toast(message, tone = "info") {
  const el = $("#toast");
  el.textContent = message;
  el.style.borderColor = tone === "error" ? "#5a2f3a" : "#2a2d3d";
  el.style.color = tone === "error" ? "#f7768e" : "#e9ecf6";
  gsap.killTweensOf(el);
  gsap.fromTo(el, { y: 20, autoAlpha: 0 }, {
    y: 0,
    autoAlpha: 1,
    duration: 0.22,
    onComplete: () => gsap.to(el, { y: 12, autoAlpha: 0, delay: 3.2, duration: 0.24 }),
  });
}

function guardConnected() {
  if (!state.connected) {
    toast("请先连接设备", "error");
    return false;
  }
  return true;
}

async function safe(label, fn) {
  try {
    return await fn();
  } catch (err) {
    toast(`${label}失败：${err.message}`, "error");
    throw err;
  }
}

function setConnected(connected, info) {
  state.connected = connected;
  $("#railStatus").textContent = connected ? "已连接" : "未连接";
  $("#railStatus").className = `rail-status ${connected ? "online" : "offline"}`;
  $("#connectBtn").innerHTML = connected ? '<i class="icon-unplug"></i><span>断开</span>' : '<i class="icon-plug"></i><span>连接</span>';
  $("#metricDevice").textContent = info ? hex(info.deviceId, 8) : "--";
  $("#metricFw").textContent = info ? hex(info.fwVersion) : "--";
  $("#metricCfg").textContent = info ? hex(info.cfgVersion) : "--";
}

function hex(value, width = 4) {
  return `0x${Number(value || 0).toString(16).toUpperCase().padStart(width, "0")}`;
}

async function refreshPorts() {
  const ports = await window.plantApi.listPorts();
  const sel = $("#portSelect");
  sel.innerHTML = "";
  if (!ports.length) {
    sel.innerHTML = '<option value="">未找到串口</option>';
    return;
  }
  for (const p of ports) {
    const opt = document.createElement("option");
    opt.value = p.path;
    opt.textContent = `${p.path}${p.manufacturer ? ` - ${p.manufacturer}` : ""}`;
    sel.appendChild(opt);
  }
}

async function connectToggle() {
  if (state.connected) {
    await safe("断开", () => window.plantApi.disconnect());
    setConnected(false);
    toast("已断开");
    return;
  }
  const path = $("#portSelect").value;
  if (!path) {
    toast("请选择串口", "error");
    return;
  }
  const info = await safe("连接", () => window.plantApi.connect({
    path,
    baudRate: Number($("#baudSelect").value),
    slave: Number($("#slaveInput").value),
  }));
  setConnected(true, info);
  toast(`连接成功，设备 ${hex(info.deviceId, 8)}`);
  await refreshStatus();
}

async function refreshStatus() {
  if (!guardConnected()) return;
  const s = await safe("读取状态", () => window.plantApi.readStatus());
  $("#metricRun").textContent = s.running ? "运行中" : "已停止";
  $("#metricSd").textContent = s.sdTotalMb ? `${Math.max(s.sdTotalMb - s.sdFreeMb, 0)} / ${s.sdTotalMb} MB` : (s.sd ? "已就绪" : "未就绪");
  const pct = s.sdTotalMb ? Math.min(100, Math.round((s.sdTotalMb - s.sdFreeMb) * 100 / s.sdTotalMb)) : 0;
  gsap.to("#capacityFill", { width: `${pct}%`, duration: 0.65, ease: "power3.out" });
  $("#metricBattery").textContent = s.batteryMv ? `${(s.batteryMv / 1000).toFixed(2)} V` : "未知";
  $("#batteryHint").textContent = batteryText(s.batteryMv);
  $("#metricError").textContent = hex(s.errorCode);
  renderSystemBits(s);
}

function batteryText(mv) {
  if (!mv) return "等待采样";
  if (mv < 3300) return "电量过低，请及时充电";
  if (mv < 3400) return "电量偏低";
  return "电量正常";
}

function renderSystemBits(s) {
  const items = [
    ["AD7124", s.ad7124],
    ["EEPROM", s.eeprom],
    ["RTC", s.rtc],
    ["SD", s.sd],
    ["RUN", s.running],
  ];
  $("#systemBits").innerHTML = items.map(([name, ok]) =>
    `<span class="chip ${ok ? "ok" : "warn"}">${name} ${ok ? "正常" : "异常"}</span>`
  ).join("");
  gsap.from("#systemBits .chip", { y: 8, autoAlpha: 0, stagger: 0.04 });
}

async function setRun(running) {
  if (!guardConnected()) return;
  await safe(running ? "启动" : "暂停", () => window.plantApi.setRun(running));
  toast(running ? "已启动采集" : "已暂停采集");
  await refreshStatus();
}

async function command(name, confirmText) {
  if (!guardConnected()) return;
  if (confirmText && !window.confirm(confirmText)) return;
  await safe("执行命令", () => window.plantApi.command(name));
  toast("命令已发送");
}

function fmtClock(dt) {
  return `${dt.year}-${pad(dt.month)}-${pad(dt.day)} ${pad(dt.hour)}:${pad(dt.minute)}:${pad(dt.second)}`;
}

function pad(v) { return String(v).padStart(2, "0"); }

async function readClock() {
  if (!guardConnected()) return;
  const dt = await safe("读取时钟", () => window.plantApi.readClock());
  $("#deviceClock").textContent = fmtClock(dt);
  gsap.fromTo("#deviceClock", { scale: 0.96, color: "#bb9af7" }, { scale: 1, color: "#7aa2f7" });
}

function localDateTimeValue(date = new Date()) {
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function dateToDto(date) {
  return {
    year: date.getFullYear(),
    month: date.getMonth() + 1,
    day: date.getDate(),
    hour: date.getHours(),
    minute: date.getMinutes(),
    second: date.getSeconds(),
  };
}

async function writeClockFromDate(date) {
  if (!guardConnected()) return;
  const dt = await safe("写入时钟", () => window.plantApi.writeClock(dateToDto(date)));
  $("#deviceClock").textContent = fmtClock(dt);
  toast("时间已同步");
}

function buildChannelChecks() {
  $("#channelChecks").innerHTML = Array.from({ length: 16 }, (_, ch) =>
    `<label><input type="checkbox" class="ch-check" value="${ch}">CH${ch}</label>`
  ).join("");
}

async function readChannels() {
  if (!guardConnected()) return;
  const channels = $$(".ch-check:checked").map((el) => Number(el.value));
  if (!channels.length) {
    toast("请先选择通道", "error");
    return;
  }
  const rows = await safe("读取通道", () => window.plantApi.readChannels(channels));
  $("#channelResults").innerHTML = [
    `<div class="table-row" style="color:var(--muted);font-size:11px"><span>通道</span><span>电压 uV</span><span>电压 V</span><span>原始码</span><span>有效</span></div>`,
    ...rows.map((r) => {
      if (!r.valid) {
        return `<div class="table-row disabled"><span>CH${r.ch}</span><span>未刷新</span><span>--</span><span>--</span><span>无效</span></div>`;
      }
      return `<div class="table-row"><span>CH${r.ch}</span><span>${r.voltageUv}</span><span>${(r.voltageUv / 1e6).toFixed(6)}</span><span>${hex(r.raw, 6)}</span><span>有效</span></div>`;
    }),
  ].join("");
  gsap.from("#channelResults .table-row", { y: 10, autoAlpha: 0, stagger: 0.035 });
}

function buildConfigShell() {
  $("#cfgGain").innerHTML = GAIN_VALUES.map((g) => `<option value="${g}">${g}</option>`).join("");
  $("#controlCards").innerHTML = Array.from({ length: 4 }, (_, index) => controlCard(index)).join("");
  const header = `<div class="config-row header"><span>通道</span><span>使能</span><span>模式</span><span>增益</span><span>量程uV</span><span>偏移uV</span><span>修正ppm</span><span>预热ms</span><span>输入</span></div>`;
  const rows = Array.from({ length: 16 }, (_, ch) => configRow(ch)).join("");
  $("#configChannels").innerHTML = header + rows;
  $$(".cfg-mode").forEach((el) => el.addEventListener("change", refreshPairStates));
  $$(".force-btn").forEach((btn) => btn.addEventListener("click", () => forceControl(btn)));
  refreshPairStates();
}

function controlCard(index) {
  return `<article class="control-card" data-control="${index}">
    <div class="control-top">
      <div class="control-name"><span class="control-led"></span><span>控制 ${index + 1}</span></div>
      <span class="control-status">未刷新</span>
    </div>
    <label class="control-enable"><input class="ctrl-enable" type="checkbox">启用自动输出</label>
    <div class="control-fields">
      <label>物理输出
        <select class="ctrl-output">
          <option value="0">OUT1</option><option value="1">OUT2</option><option value="2">OUT3</option><option value="3">OUT4</option>
        </select>
      </label>
      <label>相位(s)<input class="ctrl-phase" type="number" min="0" max="${CONFIG_TIME_MAX_SEC}" value="${index * 5}"></label>
      <label>周期(s)<input class="ctrl-interval" type="number" min="1" max="${CONFIG_TIME_MAX_SEC}" value="600"></label>
      <label>开启(s)<input class="ctrl-duration" type="number" min="1" max="${CONFIG_TIME_MAX_SEC}" value="30"></label>
    </div>
    <div class="control-actions">
      <button class="force-btn active" data-force="0">Auto</button>
      <button class="force-btn force-on" data-force="1">ON</button>
      <button class="force-btn force-off" data-force="2">OFF</button>
    </div>
  </article>`;
}

function configRow(ch) {
  return `<div class="config-row" data-ch="${ch}">
    <span>CH${ch}</span>
    <label><input class="cfg-ch-enable" type="checkbox" checked></label>
    <select class="cfg-mode"><option value="0">单端</option><option value="1">差分</option></select>
    <select class="cfg-ch-gain">${GAIN_VALUES.map((g) => `<option value="${g}">${g}</option>`).join("")}</select>
    <input class="cfg-range" type="number" min="0" max="${CONFIG_I32_LIMIT}" value="2000000">
    <input class="cfg-offset" type="number" min="-${CONFIG_I32_LIMIT}" max="${CONFIG_I32_LIMIT}" value="0">
    <input class="cfg-scale" type="number" min="-${CONFIG_I32_LIMIT}" max="${CONFIG_I32_LIMIT}" value="1000000">
    <input class="cfg-warmup" type="number" min="0" max="${CONFIG_I32_LIMIT}" value="0">
    <span class="cfg-input-label">AIN${ch}/AVSS</span>
  </div>`;
}

function refreshPairStates() {
  $$(".config-row[data-ch]").forEach((row) => {
    row.classList.remove("disabled");
    row.style.opacity = "";
    row.removeAttribute("aria-disabled");
    row.querySelectorAll("input, select").forEach((el) => { el.disabled = false; });
    row.querySelector(".cfg-input-label").textContent = `AIN${row.dataset.ch}/AVSS`;
  });

  const occupied = new Set();
  for (let ch = 0; ch < 16; ch++) {
    const row = $(`.config-row[data-ch="${ch}"]`);
    const mode = row.querySelector(".cfg-mode").value;
    const label = row.querySelector(".cfg-input-label");
    if (mode === "1") {
      const pair = ch % 2 === 0 ? ch + 1 : ch - 1;
      label.textContent = `AIN${ch}/AIN${pair}`;
      occupied.add(pair);
    }
  }

  occupied.forEach((ch) => {
    const row = $(`.config-row[data-ch="${ch}"]`);
    if (!row) return;
    row.classList.add("disabled");
    row.setAttribute("aria-disabled", "true");
    row.querySelector(".cfg-ch-enable").checked = false;
    row.querySelector(".cfg-mode").value = "0";
    row.querySelector(".cfg-input-label").textContent = "被差分占用";
    row.querySelectorAll("input, select").forEach((el) => { el.disabled = true; });
  });
}

async function readConfig() {
  if (!guardConnected()) return;
  const cfg = await safe("读取配置", () => window.plantApi.readConfig());
  state.config = cfg;
  fillConfig(cfg);
  toast("配置已读取");
}

function fillConfig(cfg) {
  $("#cfgAddr").value = cfg.modbusAddr;
  $("#cfgBaud").value = String(cfg.baudRate);
  $("#cfgSample").value = cfg.sampleIntervalSec;
  $("#cfgRecord").value = cfg.recordIntervalSec;
  $("#cfgFormat").value = String(cfg.fileFormat);
  $("#cfgGain").value = String(cfg.adcDefaultGain);
  $("#cfgAvg").checked = cfg.avgEnable;
  for (const ch of cfg.channels) {
    const row = $(`.config-row[data-ch="${ch.ch}"]`);
    row.querySelector(".cfg-ch-enable").checked = ch.enable;
    row.querySelector(".cfg-mode").value = String(ch.mode);
    row.querySelector(".cfg-ch-gain").value = String(ch.gain);
    row.querySelector(".cfg-range").value = ch.rangeUv;
    row.querySelector(".cfg-offset").value = ch.offsetUv;
    row.querySelector(".cfg-scale").value = ch.scalePpm;
    row.querySelector(".cfg-warmup").value = ch.warmupMs;
  }
  fillControls(cfg.controls || []);
  refreshPairStates();
  gsap.from(".control-card", { y: 12, autoAlpha: 0, stagger: 0.05 });
  gsap.from(".config-row[data-ch]", { x: -10, autoAlpha: 0, stagger: 0.018 });
}

function fillControls(controls) {
  for (const control of controls) {
    const row = $(`.control-card[data-control="${control.index}"]`);
    if (!row) continue;
    row.querySelector(".ctrl-enable").checked = Boolean(control.enable);
    row.querySelector(".ctrl-output").value = String(control.outputId ?? control.index);
    row.querySelector(".ctrl-interval").value = control.intervalSec ?? 600;
    row.querySelector(".ctrl-duration").value = control.onDurationSec ?? 30;
    row.querySelector(".ctrl-phase").value = control.phaseOffsetSec ?? 0;
    applyControlStatus(control);
  }
}

function applyControlStatus(control) {
  const row = $(`.control-card[data-control="${control.index}"]`);
  if (!row) return;
  row.classList.toggle("on", Boolean(control.output));
  row.dataset.enabled = control.enabled ? "1" : "0";
  const status = row.querySelector(".control-status");
  status.textContent = `${control.output ? "输出ON" : "输出OFF"} · ${control.enabled ? "已启用" : "未启用"} · ${CONTROL_FORCE_LABELS[control.force] || "AUTO"} · ${control.remainSec || 0}s`;
  status.classList.toggle("forced", Boolean(control.forced));
  row.querySelectorAll(".force-btn").forEach((btn) => {
    const force = Number(btn.dataset.force);
    btn.classList.toggle("active", force === Number(control.force || 0));
    btn.disabled = force !== 0 && !control.enabled;
  });
  gsap.fromTo(row.querySelector(".control-led"), { scale: 0.75 }, { scale: 1, duration: 0.28, ease: "back.out(1.7)", overwrite: "auto" });
}

function collectConfig(save = false) {
  return {
    modbusAddr: Number($("#cfgAddr").value),
    baudRate: Number($("#cfgBaud").value),
    avgEnable: $("#cfgAvg").checked,
    fileFormat: Number($("#cfgFormat").value),
    sampleIntervalSec: Number($("#cfgSample").value),
    recordIntervalSec: Number($("#cfgRecord").value),
    adcDefaultGain: Number($("#cfgGain").value),
    save,
    channels: $$(".config-row[data-ch]").map((row) => ({
      ch: Number(row.dataset.ch),
      enable: row.querySelector(".cfg-ch-enable").checked,
      mode: Number(row.querySelector(".cfg-mode").value),
      gain: Number(row.querySelector(".cfg-ch-gain").value),
      rangeUv: Number(row.querySelector(".cfg-range").value),
      offsetUv: Number(row.querySelector(".cfg-offset").value),
      scalePpm: Number(row.querySelector(".cfg-scale").value),
      warmupMs: Number(row.querySelector(".cfg-warmup").value),
    })),
    controls: $$(".control-card").map((row) => ({
      index: Number(row.dataset.control),
      enable: row.querySelector(".ctrl-enable").checked,
      outputId: Number(row.querySelector(".ctrl-output").value),
      intervalSec: Number(row.querySelector(".ctrl-interval").value),
      onDurationSec: Number(row.querySelector(".ctrl-duration").value),
      phaseOffsetSec: Number(row.querySelector(".ctrl-phase").value),
    })),
  };
}

function showConfigHelp() {
  const overlay = $("#configHelpOverlay");
  const panel = overlay.querySelector(".help-panel");
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  overlay.classList.remove("hidden");
  gsap.fromTo(overlay, { autoAlpha: 0 }, { autoAlpha: 1, duration: reduceMotion ? 0 : 0.18, overwrite: "auto" });
  gsap.fromTo(panel,
    { y: 18, scale: 0.98, autoAlpha: 0 },
    { y: 0, scale: 1, autoAlpha: 1, duration: reduceMotion ? 0 : 0.26, ease: "power3.out", overwrite: "auto" },
  );
}

function hideConfigHelp() {
  const overlay = $("#configHelpOverlay");
  const panel = overlay.querySelector(".help-panel");
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  gsap.to(panel, { y: 12, scale: 0.98, autoAlpha: 0, duration: reduceMotion ? 0 : 0.16, overwrite: "auto" });
  gsap.to(overlay, {
    autoAlpha: 0,
    duration: reduceMotion ? 0 : 0.18,
    overwrite: "auto",
    onComplete: () => {
      overlay.classList.add("hidden");
      gsap.set([overlay, panel], { clearProps: "all" });
    },
  });
}

function validateConfigPayload(config) {
  if (config.sampleIntervalSec < 1 || config.sampleIntervalSec > CONFIG_TIME_MAX_SEC) {
    return `采样间隔范围为 1-${CONFIG_TIME_MAX_SEC} 秒`;
  }
  if (config.recordIntervalSec < 1 || config.recordIntervalSec > CONFIG_TIME_MAX_SEC) {
    return `平均记录间隔范围为 1-${CONFIG_TIME_MAX_SEC} 秒`;
  }
  if (config.recordIntervalSec < config.sampleIntervalSec) {
    return "平均记录间隔不能小于采样间隔";
  }

  for (const ch of config.channels) {
    if (ch.rangeUv < 0 || ch.rangeUv > CONFIG_I32_LIMIT) return `CH${ch.ch} 量程超出范围`;
    if (ch.offsetUv < -CONFIG_I32_LIMIT || ch.offsetUv > CONFIG_I32_LIMIT) return `CH${ch.ch} 偏移超出范围`;
    if (ch.scalePpm < -CONFIG_I32_LIMIT || ch.scalePpm > CONFIG_I32_LIMIT) return `CH${ch.ch} 修正 ppm 超出范围`;
    if (ch.enable && ch.scalePpm === 0) return `CH${ch.ch} 修正 ppm 不能为 0`;
    if (ch.warmupMs < 0 || ch.warmupMs > CONFIG_I32_LIMIT) return `CH${ch.ch} 预热时间超出范围`;
  }

  for (const control of config.controls) {
    const name = `控制 ${control.index + 1}`;
    if (control.outputId < 0 || control.outputId > 3) return `${name} 物理输出无效`;
    if (control.phaseOffsetSec < 0 || control.phaseOffsetSec > CONFIG_TIME_MAX_SEC) return `${name} 相位范围为 0-${CONFIG_TIME_MAX_SEC} 秒`;
    if (control.intervalSec < 1 || control.intervalSec > CONFIG_TIME_MAX_SEC) return `${name} 周期范围为 1-${CONFIG_TIME_MAX_SEC} 秒`;
    if (control.onDurationSec < 1 || control.onDurationSec > CONFIG_TIME_MAX_SEC) return `${name} 开启时间范围为 1-${CONFIG_TIME_MAX_SEC} 秒`;
    if (control.onDurationSec > control.intervalSec) return `${name} 开启时间不能大于周期`;
  }

  return "";
}

async function writeConfig(save = false) {
  if (!guardConnected()) return;
  const config = collectConfig(save);
  const error = validateConfigPayload(config);
  if (error) {
    toast(error, "error");
    return;
  }
  await safe("写入配置", () => window.plantApi.writeConfig(config));
  toast(save ? "配置已写入并保存" : "配置已写入设备");
}

async function refreshControls(showToast = true) {
  if (!guardConnected()) return;
  const controls = await safe("刷新IO状态", () => window.plantApi.readControlStatuses());
  controls.forEach(applyControlStatus);
  if (showToast) toast("IO 状态已刷新");
}

async function forceControl(btn) {
  if (!guardConnected()) return;
  const row = btn.closest(".control-card");
  const index = Number(row.dataset.control);
  const force = Number(btn.dataset.force);
  if (force !== 0 && row.dataset.enabled !== "1") {
    toast("请先启用该路IO控制并写入设备，再使用强制ON/OFF", "error");
    return;
  }
  const status = await safe("设置IO强制", () => window.plantApi.setControlForce(index, force));
  applyControlStatus(status);
}

async function openDir() {
  if (!guardConnected()) return;
  const res = await safe("刷新目录", () => window.plantApi.openDir());
  state.files = res.files;
  state.selectedFiles.clear();
  renderFiles();
}

function renderFiles() {
  const total = state.files.reduce((sum, f) => sum + f.size, 0);
  $("#fileSummary").textContent = state.files.length ? `共 ${state.files.length} 个文件，${formatBytes(total)}` : "目录为空";
  $("#fileList").innerHTML = state.files.map((f) =>
    `<div class="file-row" data-row="${f.row}">
      <input type="checkbox" class="file-check" ${state.selectedFiles.has(f.row) ? "checked" : ""}>
      <span>${f.name}</span>
      <span>${formatBytes(f.size)}</span>
      <span class="chip ${f.large ? "warn" : "ok"}">${f.large ? "大文件" : "可下载"}</span>
    </div>`
  ).join("");
  $$(".file-row").forEach((row) => {
    row.addEventListener("click", (event) => {
      if (event.target.tagName !== "INPUT") row.querySelector("input").checked = !row.querySelector("input").checked;
      const idx = Number(row.dataset.row);
      if (row.querySelector("input").checked) state.selectedFiles.add(idx);
      else state.selectedFiles.delete(idx);
      row.classList.toggle("selected", state.selectedFiles.has(idx));
    });
  });
  gsap.from(".file-row", { y: 12, autoAlpha: 0, stagger: 0.035 });
}

function selectedFileObjects() {
  return state.files.filter((f) => state.selectedFiles.has(f.row));
}

async function downloadFiles() {
  if (!guardConnected()) return;
  const files = selectedFileObjects();
  if (!files.length) {
    toast("请先选择文件", "error");
    return;
  }
  const large = files.filter((f) => f.size > LARGE_FILE_BYTES);
  if (large.length) {
    const names = large.slice(0, 6).map((f) => `${f.name} ${formatBytes(f.size)}`).join("\n");
    if (!window.confirm(`选中文件中有 ${large.length} 个超过 1 MB。\n通过串口下载会比较慢，建议取回 SD 卡读取。\n\n${names}\n\n是否继续下载？`)) {
      return;
    }
  }
  showDownload(true);
  const off = window.plantApi.onDownloadProgress(updateDownloadProgress);
  try {
    const result = await safe("下载文件", () => window.plantApi.downloadFiles(files));
    if (result.canceled) toast("下载已取消");
    else if (result.failed?.length) toast(`成功 ${result.ok.length} 个，失败 ${result.failed.length} 个`, "error");
    else toast(`已下载 ${result.ok.length} 个文件`);
  } finally {
    off();
    showDownload(false);
  }
}

async function deleteFiles() {
  if (!guardConnected()) return;
  const rows = Array.from(state.selectedFiles);
  if (!rows.length) {
    toast("请先选择文件", "error");
    return;
  }
  if (!window.confirm(`确认删除选中的 ${rows.length} 个文件？此操作不可撤销。`)) return;
  const res = await safe("删除文件", () => window.plantApi.deleteFiles(rows));
  toast(`已删除 ${res.deleted} 个文件${res.failed.length ? `，失败 ${res.failed.length} 个` : ""}`);
  await openDir();
}

function showDownload(show) {
  const overlay = $("#downloadOverlay");
  if (show) {
    overlay.classList.remove("hidden");
    gsap.fromTo(".download-panel", { y: 24, autoAlpha: 0, scale: .98 }, { y: 0, autoAlpha: 1, scale: 1 });
    gsap.set("#downloadFill", { width: "0%" });
    $("#downloadText").textContent = "准备中";
  } else {
    gsap.to(".download-panel", { y: 14, autoAlpha: 0, duration: 0.18, onComplete: () => overlay.classList.add("hidden") });
  }
}

function updateDownloadProgress(p) {
  const pct = p.total ? Math.min(100, Math.round(p.received * 100 / p.total)) : 0;
  $("#downloadTitle").textContent = `正在下载 (${p.index + 1}/${p.count})`;
  $("#downloadText").textContent = `${p.name}  ${formatBytes(p.received)} / ${formatBytes(p.total)}`;
  gsap.to("#downloadFill", { width: `${pct}%`, duration: 0.2, ease: "power1.out", overwrite: "auto" });
}

function formatBytes(bytes) {
  if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${bytes} B`;
}

function bindEvents() {
  $$(".nav-item").forEach((btn) => btn.addEventListener("click", () => switchView(btn.dataset.view)));
  $("#refreshPorts").addEventListener("click", () => safe("刷新串口", refreshPorts));
  $("#connectBtn").addEventListener("click", connectToggle);
  $("#startBtn").addEventListener("click", () => setRun(true));
  $("#pauseBtn").addEventListener("click", () => setRun(false));
  $("#saveConfig").addEventListener("click", () => command("SAVE_CONFIG"));
  $("#restoreDefaults").addEventListener("click", () => command("RESTORE_DEFAULTS", "确认恢复出厂默认并保存？"));
  $("#softReset").addEventListener("click", () => command("SOFT_RESET", "确认软件复位设备？"));
  $("#retrieveConfig").addEventListener("click", readConfig);
  $("#uploadConfig").addEventListener("click", () => writeConfig(true));
  $("#readClock").addEventListener("click", readClock);
  $("#setSystemClock").addEventListener("click", () => writeClockFromDate(new Date()));
  $("#setManualClock").addEventListener("click", () => writeClockFromDate(new Date($("#manualDateTime").value)));
  $("#selectAllChannels").addEventListener("click", () => $$(".ch-check").forEach((c) => { c.checked = true; }));
  $("#selectNoChannels").addEventListener("click", () => $$(".ch-check").forEach((c) => { c.checked = false; }));
  $("#readChannels").addEventListener("click", readChannels);
  $("#readConfig").addEventListener("click", readConfig);
  $("#writeConfig").addEventListener("click", () => writeConfig(false));
  $("#writeSaveConfig").addEventListener("click", () => writeConfig(true));
  $("#configHelp").addEventListener("click", showConfigHelp);
  $("#closeConfigHelp").addEventListener("click", hideConfigHelp);
  $("#configHelpOverlay").addEventListener("click", (event) => {
    if (event.target.id === "configHelpOverlay") hideConfigHelp();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !$("#configHelpOverlay").classList.contains("hidden")) hideConfigHelp();
  });
  $("#refreshControls").addEventListener("click", () => refreshControls(true));
  $("#openDir").addEventListener("click", openDir);
  $("#downloadFiles").addEventListener("click", downloadFiles);
  $("#deleteFiles").addEventListener("click", deleteFiles);
  $("#cancelDownload").addEventListener("click", () => window.plantApi.cancelDownload());
}

function boot() {
  buildChannelChecks();
  buildConfigShell();
  $("#manualDateTime").value = localDateTimeValue();
  bindEvents();
  refreshPorts().catch((err) => toast(err.message, "error"));
  animateIntro();
}

boot();
