# -*- coding: utf-8 -*-
"""Modbus RTU 通信封装。

封装 pymodbus ModbusSerialClient，提供统一的读写接口和可读的异常处理。
兼容 pymodbus 3.0–3.6 的 slave/unit 关键字差异。
"""
import inspect

from pymodbus.client import ModbusSerialClient

from . import registers as reg


class ModbusError(Exception):
    """Modbus 通信或协议错误，携带可读信息。"""


class ModbusClient:
    """对设备的 Modbus RTU 连接封装。

    所有方法在通信失败/超时/异常响应时抛出 ModbusError。
    """

    def __init__(self):
        self._client = None
        self._slave = reg.DEFAULT_MODBUS_ADDR
        # pymodbus 3.x 用 slave=，旧版用 unit=，运行时探测
        self._slave_kw = self._detect_slave_kwarg()

    @staticmethod
    def _detect_slave_kwarg():
        """探测 read_holding_registers 接受 slave= 还是 unit=。"""
        try:
            sig = inspect.signature(
                ModbusSerialClient.read_holding_registers
            )
            if "slave" in sig.parameters:
                return "slave"
            if "unit" in sig.parameters:
                return "unit"
        except (ValueError, TypeError):
            pass
        return "slave"  # 默认按新版

    @property
    def connected(self):
        return self._client is not None and self._client.connected

    def connect(self, port, baudrate=115200, slave=1, timeout=0.5):
        """打开串口连接。失败抛 ModbusError。"""
        self.disconnect()
        self._slave = slave
        try:
            self._client = ModbusSerialClient(
                port=port,
                baudrate=baudrate,
                bytesize=8,
                parity="N",
                stopbits=1,
                timeout=timeout,
            )
        except TypeError:
            # 极旧版本可能需要 method='rtu'
            self._client = ModbusSerialClient(
                method="rtu",
                port=port,
                baudrate=baudrate,
                bytesize=8,
                parity="N",
                stopbits=1,
                timeout=timeout,
            )
        if not self._client.connect():
            self._client = None
            raise ModbusError("无法打开串口 %s" % port)

    def disconnect(self):
        if self._client is not None:
            try:
                self._client.close()
            finally:
                self._client = None

    def _kw(self):
        return {self._slave_kw: self._slave}

    def _check(self, result, what):
        """检查 pymodbus 返回值，异常响应/错误转 ModbusError。"""
        if result is None:
            raise ModbusError("%s：无响应（超时）" % what)
        if hasattr(result, "isError") and result.isError():
            # 可能是 ExceptionResponse
            code = getattr(result, "exception_code", None)
            if code is not None:
                name = reg.EXCEPTION_NAMES.get(code, "未知异常 0x%02X" % code)
                raise ModbusError("%s：设备返回异常 - %s" % (what, name))
            raise ModbusError("%s：通信错误 - %s" % (what, result))
        return result

    def _require(self):
        if not self.connected:
            raise ModbusError("未连接设备")

    # ── 读写接口 ───────────────────────────────────────────

    def read_holding(self, address, count):
        """FC03 读保持寄存器，返回 int 列表。"""
        self._require()
        r = self._client.read_holding_registers(address, count=count, **self._kw())
        self._check(r, "读保持寄存器 0x%04X×%d" % (address, count))
        return list(r.registers)

    def read_input(self, address, count):
        """FC04 读输入寄存器，返回 int 列表。"""
        self._require()
        r = self._client.read_input_registers(address, count=count, **self._kw())
        self._check(r, "读输入寄存器 0x%04X×%d" % (address, count))
        return list(r.registers)

    def write_single(self, address, value):
        """FC06 写单个寄存器。"""
        self._require()
        r = self._client.write_register(address, value & 0xFFFF, **self._kw())
        self._check(r, "写寄存器 0x%04X=0x%04X" % (address, value & 0xFFFF))

    def write_multiple(self, address, values):
        """FC16 写多个寄存器。"""
        self._require()
        vals = [v & 0xFFFF for v in values]
        r = self._client.write_registers(address, vals, **self._kw())
        self._check(r, "写多寄存器 0x%04X×%d" % (address, len(vals)))

    # ── 高层便捷 ───────────────────────────────────────────

    def read_device_info(self):
        """读设备信息 0x0000–0x0003，返回 dict。用于连接测试。"""
        regs = self.read_holding(reg.HR_DEVICE_ID_H, 4)
        return {
            "device_id": reg.join_u32(regs[0], regs[1]),
            "fw_version": regs[2],
            "cfg_version": regs[3],
        }
