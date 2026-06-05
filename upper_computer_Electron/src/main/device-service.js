const path = require("path");
const { BrowserWindow, dialog, ipcMain } = require("electron");
const { ModbusRtuClient } = require("./modbus");
const { receiveFile } = require("./ymodem");
const reg = require("./registers");

const LARGE_FILE_BYTES = 1024 * 1024;

class DeviceService {
  constructor() {
    this.client = new ModbusRtuClient();
    this.cancelDownload = false;
  }

  register() {
    ipcMain.handle("ports:list", () => ModbusRtuClient.listPorts());
    ipcMain.handle("device:connect", (_event, opts) => this.connect(opts));
    ipcMain.handle("device:disconnect", () => this.disconnect());
    ipcMain.handle("device:info", () => this.readDeviceInfo());
    ipcMain.handle("device:status", () => this.readStatus());
    ipcMain.handle("device:clock:read", () => this.readClock());
    ipcMain.handle("device:clock:write", (_event, dto) => this.writeClock(dto));
    ipcMain.handle("device:run", (_event, running) => this.setRun(running));
    ipcMain.handle("device:command", (_event, cmd) => this.command(cmd));
    ipcMain.handle("device:channels:read", (_event, channels) => this.readChannels(channels));
    ipcMain.handle("device:config:read", () => this.readConfig());
    ipcMain.handle("device:config:write", (_event, payload) => this.writeConfig(payload));
    ipcMain.handle("files:openDir", () => this.openDir());
    ipcMain.handle("files:delete", (_event, rows) => this.deleteFiles(rows));
    ipcMain.handle("files:download", (event, rows) => this.downloadFiles(event, rows));
    ipcMain.handle("files:cancelDownload", () => {
      this.cancelDownload = true;
      return true;
    });
  }

  async connect(opts) {
    await this.client.connect(opts);
    return this.readDeviceInfo();
  }

  async disconnect() {
    await this.client.disconnect();
    return true;
  }

  async readDeviceInfo() {
    const r = await this.client.readHolding(reg.HR.DEVICE_ID_H, 4);
    return {
      deviceId: reg.joinU32(r[0], r[1]),
      fwVersion: r[2],
      cfgVersion: r[3],
    };
  }

  async readStatus() {
    const r = await this.client.readInput(reg.IR.SYS_STATUS, 10);
    const cap = await this.client.readInput(reg.IR.SD_TOTAL_MB_H, 4).catch(() => [0, 0, 0, 0]);
    const status = r[0];
    return {
      running: Boolean(status & reg.SYS_BITS.RUNNING),
      ad7124: Boolean(status & reg.SYS_BITS.AD7124_READY),
      eeprom: Boolean(status & reg.SYS_BITS.EEPROM_OK),
      rtc: Boolean(status & reg.SYS_BITS.RTC_OK),
      sd: Boolean(status & reg.SYS_BITS.SD_OK),
      uptimeMs: reg.joinU32(r[1], r[2]),
      errorCode: r[3],
      sdTotalMb: reg.joinU32(cap[0], cap[1]),
      sdFreeMb: reg.joinU32(cap[2], cap[3]),
      batteryMv: r[8],
      vddaMv: r[9],
    };
  }

  async readClock() {
    const r = await this.client.readHolding(reg.HR.RTC_YEAR, 4);
    const [month, day] = reg.unpackBytes(r[1]);
    const [hour, minute] = reg.unpackBytes(r[2]);
    return { year: r[0], month, day, hour, minute, second: r[3] & 0xff };
  }

  async writeClock(dt) {
    const regs = [
      dt.year,
      reg.packBytes(dt.month, dt.day),
      reg.packBytes(dt.hour, dt.minute),
      dt.second,
    ];
    await this.client.writeMultiple(reg.HR.RTC_YEAR, regs);
    return this.readClock();
  }

  async setRun(running) {
    await this.client.writeSingle(reg.HR.RUN_ENABLE, running ? 1 : 0);
    return this.readStatus();
  }

  async command(name) {
    const cmd = reg.CMD[name];
    if (!cmd) throw new Error(`未知命令：${name}`);
    await this.client.writeSingle(reg.HR.COMMAND, cmd);
    return true;
  }

  async readChannels(channels) {
    const validMask = (await this.client.readInput(reg.IR.CH_VALID_MASK, 1))[0];
    const rows = [];
    for (const ch of channels) {
      const data = await this.client.readInput(reg.channelDataBase(ch), 4);
      rows.push({
        ch,
        voltageUv: reg.joinS32(data[0], data[1]),
        raw: reg.joinU32(data[2], data[3]) & 0xffffff,
        valid: Boolean(validMask & (1 << ch)),
      });
    }
    return rows;
  }

  async readConfig() {
    const sys = await this.client.readHolding(reg.HR.MODBUS_ADDR, 10);
    const adc = await this.client.readHolding(reg.HR.ADC_VREF_MV, 3);
    const channels = [];
    for (let ch = 0; ch < reg.CHANNEL.COUNT; ch++) {
      channels.push(this._decodeChannel(ch, await this.client.readHolding(reg.channelBase(ch), reg.CHANNEL.REG_COUNT)));
    }
    return {
      modbusAddr: sys[0],
      baudRate: reg.joinU32(sys[1], sys[2]),
      runEnable: Boolean(sys[3]),
      avgEnable: Boolean(sys[4]),
      fileFormat: sys[5],
      sampleIntervalSec: reg.joinU32(sys[6], sys[7]),
      recordIntervalSec: reg.joinU32(sys[8], sys[9]),
      adcVrefMv: adc[0],
      adcDefaultGain: adc[1],
      channels,
    };
  }

  async writeConfig(config) {
    await this.client.writeMultiple(reg.HR.BAUD_H, reg.splitU32(config.baudRate));
    await this.client.writeSingle(reg.HR.MODBUS_ADDR, config.modbusAddr);
    await this.client.writeSingle(reg.HR.AVG_ENABLE, config.avgEnable ? 1 : 0);
    await this.client.writeSingle(reg.HR.FILE_FORMAT, config.fileFormat);
    await this.client.writeMultiple(reg.HR.SAMPLE_INTERVAL_H, [
      ...reg.splitU32(config.sampleIntervalSec),
      ...reg.splitU32(config.recordIntervalSec),
    ]);
    await this.client.writeMultiple(reg.HR.ADC_VREF_MV, [
      config.adcVrefMv,
      config.adcDefaultGain,
      0,
    ]);
    for (const ch of config.channels) {
      await this.client.writeMultiple(reg.channelBase(ch.ch), this._encodeChannel(ch));
    }
    if (config.save) await this.client.writeSingle(reg.HR.COMMAND, reg.CMD.SAVE_CONFIG);
    return true;
  }

  async openDir() {
    await this.client.writeSingle(reg.HR.FILE_CMD, reg.FILE_CMD.OPEN_DIR);
    const count = (await this.client.readInput(reg.IR.FILE_XFER_STATE, 2))[1];
    const files = [];
    for (let i = 0; i < count; i++) {
      await this.client.writeSingle(reg.HR.FILE_INDEX, i);
      await this.client.writeSingle(reg.HR.FILE_CMD, reg.FILE_CMD.SELECT);
      const sizeRegs = await this.client.readInput(reg.IR.FILE_SIZE_H, 2);
      const nameRegs = await this.client.readInput(reg.IR.FILE_NAME_BASE, reg.IR.FILE_NAME_REGS);
      files.push({
        row: i,
        name: reg.decodeFileName(nameRegs),
        size: reg.joinU32(sizeRegs[0], sizeRegs[1]),
        large: reg.joinU32(sizeRegs[0], sizeRegs[1]) > LARGE_FILE_BYTES,
      });
    }
    return { files, status: await this.readStatus().catch(() => null) };
  }

  async deleteFiles(rows) {
    const sorted = [...rows].sort((a, b) => b - a);
    const failed = [];
    for (const row of sorted) {
      try {
        await this.client.writeSingle(reg.HR.FILE_INDEX, row);
        await this.client.writeSingle(reg.HR.FILE_CMD, reg.FILE_CMD.SELECT);
        await this.client.writeSingle(reg.HR.FILE_CMD, reg.FILE_CMD.DELETE);
      } catch (err) {
        failed.push({ row, error: err.message });
      }
    }
    return { deleted: sorted.length - failed.length, failed };
  }

  async downloadFiles(event, rows) {
    const win = BrowserWindow.fromWebContents(event.sender);
    const dir = await dialog.showOpenDialog(win, {
      title: rows.length > 1 ? "选择保存文件夹" : "选择保存位置",
      properties: rows.length > 1 ? ["openDirectory", "createDirectory"] : ["openDirectory", "createDirectory"],
    });
    if (dir.canceled || !dir.filePaths.length) return { canceled: true };

    const folder = dir.filePaths[0];
    this.cancelDownload = false;
    const result = { ok: [], failed: [] };
    for (let i = 0; i < rows.length; i++) {
      const file = rows[i];
      if (this.cancelDownload) return { ...result, canceled: true };
      const savePath = path.join(folder, file.name);
      try {
        await this.client.writeSingle(reg.HR.FILE_INDEX, file.row);
        await this.client.writeSingle(reg.HR.FILE_CMD, reg.FILE_CMD.SELECT);
        await this.client.writeSingle(reg.HR.FILE_CMD, reg.FILE_CMD.START);
        this.client.pauseForRawTransfer();
        await receiveFile(this.client.serialPort, savePath, {
          onProgress: (p) => {
            event.sender.send("files:downloadProgress", {
              index: i,
              count: rows.length,
              name: file.name,
              received: p.received,
              total: p.total || file.size,
            });
          },
          isCanceled: () => this.cancelDownload,
        });
        result.ok.push(file.name);
      } catch (err) {
        result.failed.push({ name: file.name, error: err.message });
      } finally {
        this.client.resumeAfterRawTransfer();
        await new Promise((resolve) => setTimeout(resolve, 220));
      }
    }
    return result;
  }

  _decodeChannel(ch, r) {
    const [enable, mode] = reg.unpackBytes(r[reg.CHANNEL.OFF_FLAGS]);
    const [_pos, _neg] = reg.unpackBytes(r[reg.CHANNEL.OFF_INPUTS]);
    const [sensorType, gain] = reg.unpackBytes(r[reg.CHANNEL.OFF_SENSOR_GAIN]);
    return {
      ch,
      enable: Boolean(enable),
      mode: mode === 1 ? 1 : 0,
      sensorType,
      gain,
      rangeUv: reg.joinU32(r[reg.CHANNEL.OFF_RANGE_H], r[reg.CHANNEL.OFF_RANGE_L]),
      offsetUv: reg.joinS32(r[reg.CHANNEL.OFF_OFFSET_H], r[reg.CHANNEL.OFF_OFFSET_L]),
      scalePpm: reg.joinS32(r[reg.CHANNEL.OFF_SCALE_H], r[reg.CHANNEL.OFF_SCALE_L]),
      warmupMs: reg.joinU32(r[reg.CHANNEL.OFF_WARMUP_H], r[reg.CHANNEL.OFF_WARMUP_L]),
    };
  }

  _encodeChannel(ch) {
    const mode = Number(ch.mode);
    const pos = ch.ch;
    const neg = mode === 1 ? (ch.ch % 2 === 0 ? ch.ch + 1 : ch.ch - 1) : reg.ADC_INPUT_AVSS;
    const regs = new Array(reg.CHANNEL.REG_COUNT).fill(0);
    regs[reg.CHANNEL.OFF_FLAGS] = reg.packBytes(ch.enable ? 1 : 0, mode);
    regs[reg.CHANNEL.OFF_INPUTS] = reg.packBytes(pos, neg);
    regs[reg.CHANNEL.OFF_SENSOR_GAIN] = reg.packBytes(0, ch.gain);
    regs[reg.CHANNEL.OFF_RANGE_H] = reg.splitU32(ch.rangeUv)[0];
    regs[reg.CHANNEL.OFF_RANGE_L] = reg.splitU32(ch.rangeUv)[1];
    regs[reg.CHANNEL.OFF_OFFSET_H] = reg.splitS32(ch.offsetUv)[0];
    regs[reg.CHANNEL.OFF_OFFSET_L] = reg.splitS32(ch.offsetUv)[1];
    regs[reg.CHANNEL.OFF_SCALE_H] = reg.splitS32(ch.scalePpm)[0];
    regs[reg.CHANNEL.OFF_SCALE_L] = reg.splitS32(ch.scalePpm)[1];
    regs[reg.CHANNEL.OFF_WARMUP_H] = reg.splitU32(ch.warmupMs)[0];
    regs[reg.CHANNEL.OFF_WARMUP_L] = reg.splitU32(ch.warmupMs)[1];
    return regs;
  }
}

module.exports = { DeviceService, LARGE_FILE_BYTES };
