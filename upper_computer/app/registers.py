# -*- coding: utf-8 -*-
"""Modbus 寄存器地址常量与编解码工具。

与 modbus_register_map.md 一一对应。所有 32 位值采用 big-endian 分割
（高字寄存器在低地址）。VIN / OFFSET / SCALE 为有符号 32 位。
"""

# ── 保持寄存器（FC03/FC06/FC16）─────────────────────────────

# 0x0000–0x0003 设备信息（只读）
HR_DEVICE_ID_H = 0x0000
HR_DEVICE_ID_L = 0x0001
HR_FW_VERSION = 0x0002
HR_CFG_VERSION = 0x0003

# 0x0010–0x0019 系统配置
HR_MODBUS_ADDR = 0x0010
HR_BAUD_H = 0x0011
HR_BAUD_L = 0x0012
HR_RUN_ENABLE = 0x0013
HR_AVG_ENABLE = 0x0014
HR_FILE_FORMAT = 0x0015
HR_SAMPLE_INTERVAL_H = 0x0016
HR_SAMPLE_INTERVAL_L = 0x0017
HR_RECORD_INTERVAL_H = 0x0018
HR_RECORD_INTERVAL_L = 0x0019

# 0x0020–0x0022 ADC 配置
HR_ADC_VREF_MV = 0x0020
HR_ADC_DEFAULT_GAIN = 0x0021
HR_ADC_DEFAULT_FILTER = 0x0022   # 保留 / 当前固件未启用：写入会被保存但不影响硬件行为

# 0x0030–0x0033 RTC 时间
HR_RTC_YEAR = 0x0030
HR_RTC_MONDAY = 0x0031   # [15:8]=月 [7:0]=日
HR_RTC_HMIN = 0x0032     # [15:8]=时 [7:0]=分
HR_RTC_SEC = 0x0033

# 0x0040 控制命令（只写）
HR_COMMAND = 0x0040
CMD_SAVE_CONFIG = 0x0001
CMD_RESTORE_DEFAULTS = 0x0002
CMD_SOFT_RESET = 0x0003

# 0x0050–0x0053 控制输出手动强制（不保存 EEPROM）
HR_FORCE_BASE = 0x0050   # 控制路 n: 0x0050 + n
FORCE_AUTO = 0
FORCE_ON = 1
FORCE_OFF = 2

# 0x0060–0x0061 文件传输控制
HR_FILE_CMD = 0x0060
HR_FILE_INDEX = 0x0061
FILE_CMD_OPEN_DIR = 0x0001
FILE_CMD_SELECT = 0x0002
FILE_CMD_START = 0x0003
FILE_CMD_DELETE = 0x0004

# 0x0100–0x01FF 通道配置（16 通道 × 16 寄存器）
HR_CHANNEL_BASE = 0x0100
CHANNEL_STRIDE = 0x10
# 通道内偏移
CH_OFF_FLAGS = 0x0      # [15:8]=enable [7:0]=mode(0单端 1差分)
CH_OFF_INPUTS = 0x1     # [15:8]=positive_input [7:0]=negative_input
CH_OFF_SENSOR_GAIN = 0x2  # [15:8]=sensor_type [7:0]=gain
CH_OFF_FILTER = 0x3     # 保留 / 当前固件未启用：所有通道固定 Sinc4 + ~10 SPS + 50/60Hz 抑制
CH_OFF_RANGE_H = 0x4
CH_OFF_RANGE_L = 0x5
CH_OFF_OFFSET_H = 0x6   # 有符号
CH_OFF_OFFSET_L = 0x7
CH_OFF_SCALE_H = 0x8    # 有符号
CH_OFF_SCALE_L = 0x9
CH_OFF_WARMUP_H = 0xA
CH_OFF_WARMUP_L = 0xB
CHANNEL_REG_COUNT = 0xC  # 实际使用的寄存器数（0x0–0xB）

# 0x0200–0x022F 控制输出配置（4 路 × 12 寄存器）
HR_CONTROL_BASE = 0x0200
CONTROL_STRIDE = 0x0C

# ── 输入寄存器（FC04，只读，实时数据）──────────────────────

# 0x0000–0x003F ADC 通道测量值（16 通道 × 4 寄存器）
IR_CHANNEL_BASE = 0x0000
CHANNEL_DATA_STRIDE = 4
IR_CH_VIN_H = 0   # 有符号
IR_CH_VIN_L = 1
IR_CH_RAW_H = 2
IR_CH_RAW_L = 3

# 0x0040–0x0047 控制输出状态（4 路 × 2 寄存器）
IR_CONTROL_STATUS_BASE = 0x0040

# 0x0048–0x004F 数据新鲜度
IR_CH_VALID_MASK = 0x0048
IR_SAMPLE_COUNT = 0x0049
IR_LAST_SMP_YEAR = 0x004A
IR_LAST_SMP_MONDAY = 0x004B
IR_LAST_SMP_HMIN = 0x004C
IR_LAST_SMP_SEC = 0x004D

# 0x0050–0x0053 系统状态
IR_SYS_STATUS = 0x0050
IR_UPTIME_H = 0x0051
IR_UPTIME_L = 0x0052
IR_ERR_CODE = 0x0053

# SYS_STATUS 位定义
SYS_BIT_AD7124_READY = 0x01
SYS_BIT_EEPROM_OK = 0x02
SYS_BIT_RTC_OK = 0x04
SYS_BIT_SD_OK = 0x08
SYS_BIT_RUNNING = 0x10

# 0x0060–0x0074 文件传输状态
IR_FILE_XFER_STATE = 0x0060
IR_FILE_COUNT = 0x0061
IR_FILE_SIZE_H = 0x0062
IR_FILE_SIZE_L = 0x0063
IR_FILE_NAME_BASE = 0x0064
IR_FILE_NAME_REGS = 16   # 0x0064–0x0073
IR_FILE_ERR = 0x0074

# 常量
CHANNEL_COUNT = 16
CONTROL_COUNT = 4
ADC_INPUT_AVSS = 17

# 配置版本/魔数（来自固件 app_config.h）
DEFAULT_MODBUS_ADDR = 1
DEFAULT_BAUDRATE = 115200

# 增益可选值（PGA）
GAIN_VALUES = [1, 2, 4, 8, 16, 32, 64, 128]

# 文件格式
FILE_FORMAT_NAMES = {0: "CSV", 1: "DAT", 2: "TXT"}

# 通道模式
CHANNEL_MODE_NAMES = {0: "单端", 1: "差分"}

# 错误码（ERR_CODE）
ERR_CODE_NAMES = {
    0x0000: "正常",
    0x0001: "AD7124 通信/采样故障",
    0x0002: "EEPROM 读写故障",
    0x0003: "RTC 故障",
    0x0004: "SD 卡挂载/写入故障",
    0x0005: "配置 CRC 校验失败，已回退默认",
    0x00FF: "未知错误",
}

# 文件传输状态
FILE_XFER_STATE_NAMES = {
    0: "空闲", 1: "目录就绪", 2: "已选中",
    3: "传输中", 4: "完成", 5: "错误",
}

# Modbus 异常码
EXCEPTION_NAMES = {
    0x01: "非法功能码",
    0x02: "非法寄存器地址",
    0x03: "非法数据值",
    0x04: "从机设备故障",
}


# ── 32 位编解码（big-endian 分割：高字在低地址）─────────────

def split_u32(value):
    """拆 32 位无符号为 (高字, 低字)。"""
    value &= 0xFFFFFFFF
    return (value >> 16) & 0xFFFF, value & 0xFFFF


def split_s32(value):
    """拆 32 位有符号为 (高字, 低字)。"""
    return split_u32(value & 0xFFFFFFFF)


def join_u32(hi, lo):
    """合 (高字, 低字) 为 32 位无符号。"""
    return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)


def join_s32(hi, lo):
    """合 (高字, 低字) 为 32 位有符号。"""
    v = join_u32(hi, lo)
    if v & 0x80000000:
        v -= 0x100000000
    return v


def split_u16(value):
    """16 位无符号钳到合法范围。"""
    return value & 0xFFFF


def pack_bytes_hi_lo(high_byte, low_byte):
    """组合两个字节为一个 16 位寄存器（高字节在前）。"""
    return ((high_byte & 0xFF) << 8) | (low_byte & 0xFF)


def unpack_bytes_hi_lo(reg):
    """拆一个 16 位寄存器为 (高字节, 低字节)。"""
    return (reg >> 8) & 0xFF, reg & 0xFF


def channel_base(n):
    """通道 n 配置基地址。"""
    return HR_CHANNEL_BASE + n * CHANNEL_STRIDE


def channel_data_base(n):
    """通道 n 实时数据（输入寄存器）基地址。"""
    return IR_CHANNEL_BASE + n * CHANNEL_DATA_STRIDE


def control_base(n):
    """控制路 n 配置基地址。"""
    return HR_CONTROL_BASE + n * CONTROL_STRIDE


def decode_file_name(regs):
    """从文件名寄存器（每寄存器 2 字符，高字节先）解码 ASCII 文件名。"""
    chars = []
    for reg in regs:
        hi, lo = unpack_bytes_hi_lo(reg)
        if hi == 0:
            break
        chars.append(chr(hi))
        if lo == 0:
            break
        chars.append(chr(lo))
    return "".join(chars)

