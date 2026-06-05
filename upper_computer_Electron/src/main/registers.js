const HR = {
  DEVICE_ID_H: 0x0000,
  MODBUS_ADDR: 0x0010,
  BAUD_H: 0x0011,
  BAUD_L: 0x0012,
  RUN_ENABLE: 0x0013,
  AVG_ENABLE: 0x0014,
  FILE_FORMAT: 0x0015,
  SAMPLE_INTERVAL_H: 0x0016,
  RECORD_INTERVAL_H: 0x0018,
  ADC_VREF_MV: 0x0020,
  ADC_DEFAULT_GAIN: 0x0021,
  RTC_YEAR: 0x0030,
  COMMAND: 0x0040,
  CONTROL_FORCE_BASE: 0x0050,
  FILE_CMD: 0x0060,
  FILE_INDEX: 0x0061,
  CHANNEL_BASE: 0x0100,
  CHANNEL_STRIDE: 0x10,
  CONTROL_BASE: 0x0200,
  CONTROL_STRIDE: 0x0c,
};

const IR = {
  CHANNEL_BASE: 0x0000,
  CHANNEL_STRIDE: 4,
  CONTROL_STATUS_BASE: 0x0040,
  CONTROL_STATUS_STRIDE: 2,
  CH_VALID_MASK: 0x0048,
  SYS_STATUS: 0x0050,
  SD_TOTAL_MB_H: 0x0054,
  BATTERY_MV: 0x0058,
  FILE_XFER_STATE: 0x0060,
  FILE_COUNT: 0x0061,
  FILE_SIZE_H: 0x0062,
  FILE_NAME_BASE: 0x0064,
  FILE_NAME_REGS: 16,
};

const CMD = {
  SAVE_CONFIG: 0x0001,
  RESTORE_DEFAULTS: 0x0002,
  SOFT_RESET: 0x0003,
};

const FILE_CMD = {
  OPEN_DIR: 0x0001,
  SELECT: 0x0002,
  START: 0x0003,
  DELETE: 0x0004,
};

const CHANNEL = {
  COUNT: 16,
  REG_COUNT: 0x0c,
  OFF_FLAGS: 0,
  OFF_INPUTS: 1,
  OFF_SENSOR_GAIN: 2,
  OFF_FILTER: 3,
  OFF_RANGE_H: 4,
  OFF_RANGE_L: 5,
  OFF_OFFSET_H: 6,
  OFF_OFFSET_L: 7,
  OFF_SCALE_H: 8,
  OFF_SCALE_L: 9,
  OFF_WARMUP_H: 10,
  OFF_WARMUP_L: 11,
};

const CONTROL = {
  COUNT: 4,
  REG_COUNT: 0x0c,
  OFF_FLAGS: 0,
  OFF_INTERVAL_H: 2,
  OFF_INTERVAL_L: 3,
  OFF_ON_DURATION_H: 4,
  OFF_ON_DURATION_L: 5,
  OFF_PHASE_H: 6,
  OFF_PHASE_L: 7,
  FORCE_AUTO: 0,
  FORCE_ON: 1,
  FORCE_OFF: 2,
};

const GAIN_VALUES = [1, 2, 4, 8, 16, 32, 64, 128];
const ADC_INPUT_AVSS = 17;

const SYS_BITS = {
  AD7124_READY: 0x01,
  EEPROM_OK: 0x02,
  RTC_OK: 0x04,
  SD_OK: 0x08,
  RUNNING: 0x10,
};

const EXCEPTION_NAMES = {
  0x01: "非法功能码",
  0x02: "非法寄存器地址",
  0x03: "非法数据值",
  0x04: "从机设备故障",
};

function splitU32(value) {
  const v = Number(value) >>> 0;
  return [(v >>> 16) & 0xffff, v & 0xffff];
}

function joinU32(hi, lo) {
  return (((hi & 0xffff) << 16) | (lo & 0xffff)) >>> 0;
}

function joinS32(hi, lo) {
  const v = joinU32(hi, lo);
  return v & 0x80000000 ? v - 0x100000000 : v;
}

function splitS32(value) {
  return splitU32(Number(value) >>> 0);
}

function packBytes(hi, lo) {
  return ((hi & 0xff) << 8) | (lo & 0xff);
}

function unpackBytes(reg) {
  return [(reg >>> 8) & 0xff, reg & 0xff];
}

function channelBase(ch) {
  return HR.CHANNEL_BASE + ch * HR.CHANNEL_STRIDE;
}

function channelDataBase(ch) {
  return IR.CHANNEL_BASE + ch * IR.CHANNEL_STRIDE;
}

function controlBase(index) {
  return HR.CONTROL_BASE + index * HR.CONTROL_STRIDE;
}

function controlStatusBase(index) {
  return IR.CONTROL_STATUS_BASE + index * IR.CONTROL_STATUS_STRIDE;
}

function decodeFileName(regs) {
  const chars = [];
  for (const item of regs) {
    const [hi, lo] = unpackBytes(item);
    if (hi === 0) break;
    chars.push(String.fromCharCode(hi));
    if (lo === 0) break;
    chars.push(String.fromCharCode(lo));
  }
  return chars.join("");
}

module.exports = {
  HR,
  IR,
  CMD,
  FILE_CMD,
  CHANNEL,
  CONTROL,
  GAIN_VALUES,
  ADC_INPUT_AVSS,
  SYS_BITS,
  EXCEPTION_NAMES,
  splitU32,
  splitS32,
  joinU32,
  joinS32,
  packBytes,
  unpackBytes,
  channelBase,
  channelDataBase,
  controlBase,
  controlStatusBase,
  decodeFileName,
};
