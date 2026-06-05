const { SerialPort } = require("serialport");
const { EXCEPTION_NAMES } = require("./registers");

class ModbusError extends Error {
  constructor(message) {
    super(message);
    this.name = "ModbusError";
  }
}

function crc16(buffer) {
  let crc = 0xffff;
  for (const b of buffer) {
    crc ^= b;
    for (let i = 0; i < 8; i++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xa001) : (crc >> 1);
    }
  }
  return crc & 0xffff;
}

function appendCrc(payload) {
  const out = Buffer.alloc(payload.length + 2);
  payload.copy(out, 0);
  const crc = crc16(payload);
  out[out.length - 2] = crc & 0xff;
  out[out.length - 1] = (crc >> 8) & 0xff;
  return out;
}

function readU16BE(buf, offset) {
  return ((buf[offset] << 8) | buf[offset + 1]) & 0xffff;
}

class ModbusRtuClient {
  constructor() {
    this.port = null;
    this.slave = 1;
    this.timeoutMs = 1000;
    this.retries = 2;
    this.rx = Buffer.alloc(0);
    this.pending = null;
    this.queue = Promise.resolve();
  }

  static async listPorts() {
    const ports = await SerialPort.list();
    return ports.map((p) => ({
      path: p.path,
      manufacturer: p.manufacturer || "",
      friendlyName: p.friendlyName || p.path,
      serialNumber: p.serialNumber || "",
    }));
  }

  get connected() {
    return Boolean(this.port && this.port.isOpen);
  }

  get serialPort() {
    return this.port;
  }

  async connect({ path, baudRate = 115200, slave = 1, timeoutMs = 1000, retries = 2, settleMs = 250 }) {
    await this.disconnect();
    this.slave = Number(slave);
    this.timeoutMs = Number(timeoutMs);
    this.retries = Math.max(0, Number(retries));
    this.rx = Buffer.alloc(0);
    this.pending = null;
    this.queue = Promise.resolve();
    this.port = new SerialPort({
      path,
      baudRate: Number(baudRate),
      dataBits: 8,
      parity: "none",
      stopBits: 1,
      autoOpen: false,
    });
    this.port.on("data", (chunk) => this._onData(chunk));
    this.port.on("error", (err) => {
      if (this.pending) this.pending.reject(new ModbusError(err.message));
    });
    await new Promise((resolve, reject) => {
      this.port.open((err) => err ? reject(new ModbusError(err.message)) : resolve());
    });
    if (settleMs > 0) {
      await new Promise((resolve) => setTimeout(resolve, settleMs));
      this.rx = Buffer.alloc(0);
    }
  }

  async disconnect() {
    if (this.pending) {
      clearTimeout(this.pending.timeout);
      this.pending.reject(new ModbusError("连接已断开"));
      this.pending = null;
    }
    this.queue = Promise.resolve();
    if (!this.port) return;
    const p = this.port;
    this.port = null;
    this.rx = Buffer.alloc(0);
    if (!p.isOpen) return;
    await new Promise((resolve) => p.close(() => resolve()));
  }

  readHolding(address, count) {
    return this._enqueue(0x03, address, count);
  }

  readInput(address, count) {
    return this._enqueue(0x04, address, count);
  }

  writeSingle(address, value) {
    const frame = Buffer.from([
      this.slave,
      0x06,
      (address >> 8) & 0xff,
      address & 0xff,
      (value >> 8) & 0xff,
      value & 0xff,
    ]);
    return this._request(appendCrc(frame), 0x06, (body) => {
      const gotAddr = readU16BE(body, 2);
      const gotVal = readU16BE(body, 4);
      if (gotAddr !== address || gotVal !== (value & 0xffff)) {
        throw new ModbusError("写单寄存器应答不匹配");
      }
      return true;
    });
  }

  writeMultiple(address, values) {
    const count = values.length;
    const frame = Buffer.alloc(7 + count * 2);
    frame[0] = this.slave;
    frame[1] = 0x10;
    frame[2] = (address >> 8) & 0xff;
    frame[3] = address & 0xff;
    frame[4] = (count >> 8) & 0xff;
    frame[5] = count & 0xff;
    frame[6] = count * 2;
    values.forEach((v, i) => {
      frame[7 + i * 2] = (v >> 8) & 0xff;
      frame[8 + i * 2] = v & 0xff;
    });
    return this._request(appendCrc(frame), 0x10, (body) => {
      const gotAddr = readU16BE(body, 2);
      const gotCount = readU16BE(body, 4);
      if (gotAddr !== address || gotCount !== count) {
        throw new ModbusError("写多寄存器应答不匹配");
      }
      return true;
    });
  }

  pauseForRawTransfer() {
    this.rx = Buffer.alloc(0);
  }

  resumeAfterRawTransfer() {
    this.rx = Buffer.alloc(0);
  }

  _enqueue(functionCode, address, count) {
    const frame = Buffer.from([
      this.slave,
      functionCode,
      (address >> 8) & 0xff,
      address & 0xff,
      (count >> 8) & 0xff,
      count & 0xff,
    ]);
    return this._request(appendCrc(frame), functionCode, (body) => {
      const byteCount = body[2];
      if (byteCount !== count * 2) {
        throw new ModbusError("读取应答长度不匹配");
      }
      const regs = [];
      for (let i = 0; i < count; i++) {
        regs.push(readU16BE(body, 3 + i * 2));
      }
      return regs;
    });
  }

  _request(frame, functionCode, parser) {
    const job = this.queue
      .catch(() => {})
      .then(() => this._sendWithRetry(frame, functionCode, parser));
    this.queue = job.catch(() => {});
    return job;
  }

  async _sendWithRetry(frame, functionCode, parser) {
    let lastErr;
    for (let attempt = 0; attempt <= this.retries; attempt++) {
      try {
        return await this._send(frame, functionCode, parser);
      } catch (err) {
        lastErr = err;
        if (!(err instanceof ModbusError) || !this.connected) throw err;
        if (attempt < this.retries) {
          await new Promise((resolve) => setTimeout(resolve, 50));
          this.rx = Buffer.alloc(0);
        }
      }
    }
    throw lastErr;
  }

  _send(frame, functionCode, parser) {
    if (!this.connected) {
      return Promise.reject(new ModbusError("未连接设备"));
    }
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending = null;
        reject(new ModbusError("设备无响应（超时）"));
      }, this.timeoutMs);
      this.pending = { functionCode, parser, resolve, reject, timeout };
      this.rx = Buffer.alloc(0);
      this.port.write(frame, (err) => {
        if (err) {
          clearTimeout(timeout);
          this.pending = null;
          reject(new ModbusError(err.message));
          return;
        }
        this.port.drain(() => {});
      });
    });
  }

  _onData(chunk) {
    if (!this.pending) return;
    this.rx = Buffer.concat([this.rx, chunk]);
    this._tryParse();
  }

  _tryParse() {
    const p = this.pending;
    if (!p || this.rx.length < 5) return;

    const fc = this.rx[1];
    let expectedLength = 0;
    if (fc === (p.functionCode | 0x80)) {
      expectedLength = 5;
    } else if (fc === 0x03 || fc === 0x04) {
      expectedLength = 3 + this.rx[2] + 2;
    } else if (fc === 0x06 || fc === 0x10) {
      expectedLength = 8;
    } else {
      expectedLength = this.rx.length;
    }

    if (this.rx.length < expectedLength) return;
    const frame = this.rx.subarray(0, expectedLength);
    const body = frame.subarray(0, frame.length - 2);
    const gotCrc = frame[frame.length - 2] | (frame[frame.length - 1] << 8);
    const calcCrc = crc16(body);
    clearTimeout(p.timeout);
    this.pending = null;

    if (gotCrc !== calcCrc) {
      p.reject(new ModbusError("CRC 校验失败"));
      return;
    }
    if (body[0] !== this.slave) {
      p.reject(new ModbusError("从机地址不匹配"));
      return;
    }
    if (body[1] === (p.functionCode | 0x80)) {
      const code = body[2];
      p.reject(new ModbusError(`设备返回异常 - ${EXCEPTION_NAMES[code] || `0x${code.toString(16)}`}`));
      return;
    }
    try {
      p.resolve(p.parser(body));
    } catch (err) {
      p.reject(err);
    }
  }
}

module.exports = { ModbusRtuClient, ModbusError, crc16 };
