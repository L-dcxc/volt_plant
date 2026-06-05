const fs = require("fs");

const SOH = 0x01;
const STX = 0x02;
const EOT = 0x04;
const ACK = 0x06;
const NAK = 0x15;
const CAN = 0x18;
const C = 0x43;

class YModemError extends Error {
  constructor(message) {
    super(message);
    this.name = "YModemError";
  }
}

function crc16(data) {
  let crc = 0;
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) {
      crc = (crc & 0x8000) ? (((crc << 1) ^ 0x1021) & 0xffff) : ((crc << 1) & 0xffff);
    }
  }
  return crc & 0xffff;
}

class ByteReader {
  constructor(port) {
    this.port = port;
    this.buf = Buffer.alloc(0);
    this.waiters = [];
    this.onData = (chunk) => {
      this.buf = Buffer.concat([this.buf, chunk]);
      this._pump();
    };
    port.on("data", this.onData);
  }

  dispose() {
    this.port.off("data", this.onData);
  }

  read(n, timeoutMs) {
    if (this.buf.length >= n) {
      const out = this.buf.subarray(0, n);
      this.buf = this.buf.subarray(n);
      return Promise.resolve(out);
    }
    return new Promise((resolve, reject) => {
      const waiter = {
        n,
        resolve,
        reject,
        timer: setTimeout(() => {
          this.waiters = this.waiters.filter((w) => w !== waiter);
          reject(new YModemError(`读超时 (${n} bytes)`));
        }, timeoutMs),
      };
      this.waiters.push(waiter);
      this._pump();
    });
  }

  async readOne(timeoutMs) {
    const b = await this.read(1, timeoutMs);
    return b[0];
  }

  _pump() {
    for (;;) {
      const waiter = this.waiters[0];
      if (!waiter || this.buf.length < waiter.n) break;
      this.waiters.shift();
      clearTimeout(waiter.timer);
      const out = this.buf.subarray(0, waiter.n);
      this.buf = this.buf.subarray(waiter.n);
      waiter.resolve(out);
    }
  }
}

function writeByte(port, byte) {
  return new Promise((resolve, reject) => {
    port.write(Buffer.from([byte]), (err) => {
      if (err) reject(new YModemError(err.message));
      else port.drain(() => resolve());
    });
  });
}

async function drainForLeader(reader, timeoutMs) {
  const end = Date.now() + timeoutMs;
  while (Date.now() < end) {
    try {
      const b = await reader.readOne(Math.min(500, Math.max(1, end - Date.now())));
      if ([SOH, STX, EOT, CAN].includes(b)) return b;
    } catch (_err) {
      // keep waiting until the overall timeout expires
    }
  }
  throw new YModemError("等待块首字节超时");
}

async function readPacket(reader, leader) {
  const blockSize = leader === SOH ? 128 : 1024;
  const rest = await reader.read(blockSize + 4, 3000);
  const seq = rest[0];
  const seqInv = rest[1];
  const data = rest.subarray(2, 2 + blockSize);
  const gotCrc = (rest[2 + blockSize] << 8) | rest[3 + blockSize];
  if (seqInv !== (0xff ^ seq) || gotCrc !== crc16(data)) {
    throw new YModemError("数据块校验失败");
  }
  return { seq, data };
}

async function receiveFile(port, savePath, { onProgress, isCanceled } = {}) {
  const reader = new ByteReader(port);
  let out = null;
  try {
    let header = null;
    for (let attempt = 0; attempt < 10; attempt++) {
      if (isCanceled && isCanceled()) throw new YModemError("用户取消");
      await writeByte(port, C);
      try {
        const leader = await drainForLeader(reader, attempt === 0 ? 10000 : 1500);
        if (leader === CAN) throw new YModemError("发送端取消传输");
        if (leader === SOH || leader === STX) {
          const packet = await readPacket(reader, leader);
          if (packet.seq !== 0) {
            await writeByte(port, NAK);
            continue;
          }
          header = packet.data;
          await writeByte(port, ACK);
          break;
        }
      } catch (_err) {
        // resend C
      }
    }
    if (!header) throw new YModemError("未收到 YMODEM 头块");

    const nul = header.indexOf(0);
    const fileName = header.subarray(0, Math.max(0, nul)).toString("ascii");
    const tail = header.subarray(nul + 1);
    const sizeEnd = tail.indexOf(0);
    const sizeText = tail.subarray(0, sizeEnd >= 0 ? sizeEnd : tail.length).toString("ascii").trim();
    const totalSize = Number.parseInt(sizeText || "0", 10) || 0;
    let received = 0;
    let expectedSeq = 1;
    out = fs.createWriteStream(savePath);

    if (onProgress) onProgress({ received: 0, total: totalSize, fileName });
    await writeByte(port, C);

    for (;;) {
      if (isCanceled && isCanceled()) throw new YModemError("用户取消");
      const leader = await drainForLeader(reader, 10000);
      if (leader === EOT) {
        await writeByte(port, NAK);
        try {
          const second = await reader.readOne(2000);
          if (second === EOT) await writeByte(port, ACK);
        } catch (_err) {
          await writeByte(port, ACK);
        }
        await writeByte(port, C);
        try {
          const tailLeader = await drainForLeader(reader, 2000);
          if (tailLeader === SOH || tailLeader === STX) {
            await readPacket(reader, tailLeader);
            await writeByte(port, ACK);
          }
        } catch (_err) {
          // tolerant sender/client compatibility
        }
        await new Promise((resolve) => out.end(resolve));
        return { fileName, size: received };
      }
      if (leader === CAN) throw new YModemError("发送端取消传输");
      const packet = await readPacket(reader, leader);
      if (packet.seq === ((expectedSeq - 1) & 0xff)) {
        await writeByte(port, ACK);
        continue;
      }
      if (packet.seq !== (expectedSeq & 0xff)) {
        await writeByte(port, NAK);
        continue;
      }
      const remaining = totalSize > 0 ? totalSize - received : packet.data.length;
      const writable = Math.min(Math.max(remaining, 0), packet.data.length);
      if (writable > 0) {
        out.write(packet.data.subarray(0, writable));
        received += writable;
      }
      expectedSeq = (expectedSeq + 1) & 0xff;
      await writeByte(port, ACK);
      if (onProgress) onProgress({ received, total: totalSize, fileName });
    }
  } catch (err) {
    try {
      await writeByte(port, CAN);
      await writeByte(port, CAN);
    } catch (_e) {
      // ignore
    }
    if (out) {
      await new Promise((resolve) => out.end(resolve));
      try { fs.unlinkSync(savePath); } catch (_e) { /* ignore */ }
    }
    throw err;
  } finally {
    reader.dispose();
  }
}

module.exports = { receiveFile, YModemError };
