# -*- coding: utf-8 -*-
"""时钟标签页：读取设备 RTC、手动设定、设为系统时间。

RTC 寄存器 0x0030–0x0033（FC03/FC16）：
  0x0030 年
  0x0031 [15:8]月 [7:0]日
  0x0032 [15:8]时 [7:0]分
  0x0033 秒
写 RTC_SEC 后固件立即更新，故用 FC16 一次写 4 个寄存器。
"""

from datetime import datetime

from PyQt5.QtCore import QDateTime
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QFormLayout,
    QDateTimeEdit, QPushButton, QLabel, QMessageBox
)

from .. import registers as reg
from ..modbus_client import ModbusError


class ClockTab(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        self._buttons = []
        self._build_ui()
        self.ctx.connectionChanged.connect(self._on_conn_changed)
        self._on_conn_changed(False)

    def _build_ui(self):
        layout = QVBoxLayout(self)

        box = QGroupBox("设备时钟")
        form = QFormLayout(box)

        self.lbl_device_time = QLabel("-")
        form.addRow("设备当前时间：", self.lbl_device_time)

        self.dt_edit = QDateTimeEdit()
        self.dt_edit.setDisplayFormat("yyyy-MM-dd HH:mm:ss")
        self.dt_edit.setCalendarPopup(True)
        self.dt_edit.setDateTime(QDateTime.currentDateTime())
        form.addRow("设定时间：", self.dt_edit)

        btn_row = QHBoxLayout()
        self.btn_read = self._btn("读取设备时间", self._on_read)
        self.btn_sys = self._btn("设为系统时间", self._on_set_system)
        self.btn_manual = self._btn("写入设定时间", self._on_set_manual)
        for b in (self.btn_read, self.btn_sys, self.btn_manual):
            btn_row.addWidget(b)
        form.addRow(btn_row)

        layout.addWidget(box)
        layout.addStretch(1)

    def _btn(self, text, slot):
        b = QPushButton(text)
        b.clicked.connect(slot)
        self._buttons.append(b)
        return b

    def _on_conn_changed(self, connected):
        for b in self._buttons:
            b.setEnabled(connected)

    def _on_read(self):
        try:
            r = self.ctx.client.read_holding(reg.HR_RTC_YEAR, 4)
        except ModbusError as e:
            QMessageBox.critical(self, "读取失败", str(e))
            return
        year = r[0]
        month, day = reg.unpack_bytes_hi_lo(r[1])
        hour, minute = reg.unpack_bytes_hi_lo(r[2])
        second = r[3] & 0xFF
        try:
            text = "%04d-%02d-%02d %02d:%02d:%02d" % (
                year, month, day, hour, minute, second)
        except Exception:
            text = "无效时间"
        self.lbl_device_time.setText(text)
        self.ctx.notify("已读取设备时间")

    def _on_set_system(self):
        self._write_time(datetime.now())

    def _on_set_manual(self):
        py_dt = self.dt_edit.dateTime().toPyDateTime()
        self._write_time(py_dt)

    def _write_time(self, dt):
        regs = [
            dt.year,
            reg.pack_bytes_hi_lo(dt.month, dt.day),
            reg.pack_bytes_hi_lo(dt.hour, dt.minute),
            dt.second,
        ]
        try:
            self.ctx.client.write_multiple(reg.HR_RTC_YEAR, regs)
        except ModbusError as e:
            QMessageBox.critical(self, "写入失败", str(e))
            return
        self.ctx.notify("时间已写入：%s" % dt.strftime("%Y-%m-%d %H:%M:%S"))
        self._on_read()
