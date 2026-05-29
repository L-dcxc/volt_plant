# -*- coding: utf-8 -*-
"""运行控制标签页：上传/取回程序、启动/暂停、保存/恢复/复位、系统状态。

甲方"程序"= 采集配置：
  上传程序 = 写全部配置 + 保存到 EEPROM（委托 config_tab）
  取回程序 = 从设备读全部配置（委托 config_tab）
  启动程序 = RUN_ENABLE=1
  暂停运行 = RUN_ENABLE=0
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QPushButton,
    QFormLayout, QLabel, QMessageBox
)

from .. import registers as reg
from ..modbus_client import ModbusError


class ControlTab(QWidget):
    def __init__(self, ctx, config_tab):
        super().__init__()
        self.ctx = ctx
        self.config_tab = config_tab
        self._buttons = []
        self._build_ui()
        self.ctx.connectionChanged.connect(self._on_conn_changed)
        self._on_conn_changed(False)

    def _build_ui(self):
        layout = QVBoxLayout(self)

        prog_box = QGroupBox("程序控制")
        row = QHBoxLayout(prog_box)
        self.btn_upload = self._btn("上传程序", self._on_upload)
        self.btn_retrieve = self._btn("取回程序", self._on_retrieve)
        self.btn_start = self._btn("启动程序", self._on_start)
        self.btn_pause = self._btn("暂停运行", self._on_pause)
        for b in (self.btn_upload, self.btn_retrieve, self.btn_start, self.btn_pause):
            row.addWidget(b)
        layout.addWidget(prog_box)

        cfg_box = QGroupBox("配置管理")
        crow = QHBoxLayout(cfg_box)
        self.btn_save = self._btn("保存到 EEPROM", self._on_save)
        self.btn_restore = self._btn("恢复出厂", self._on_restore)
        self.btn_reset = self._btn("软件复位", self._on_reset)
        for b in (self.btn_save, self.btn_restore, self.btn_reset):
            crow.addWidget(b)
        layout.addWidget(cfg_box)

        status_box = QGroupBox("系统状态")
        sform = QFormLayout(status_box)
        self.lbl_run = QLabel("-")
        self.lbl_ad = QLabel("-")
        self.lbl_eeprom = QLabel("-")
        self.lbl_rtc = QLabel("-")
        self.lbl_sd = QLabel("-")
        self.lbl_err = QLabel("-")
        sform.addRow("运行状态：", self.lbl_run)
        sform.addRow("AD7124：", self.lbl_ad)
        sform.addRow("EEPROM：", self.lbl_eeprom)
        sform.addRow("RTC：", self.lbl_rtc)
        sform.addRow("SD 卡：", self.lbl_sd)
        sform.addRow("错误码：", self.lbl_err)
        self.btn_refresh = self._btn("刷新状态", self._on_refresh_status)
        sform.addRow(self.btn_refresh)
        layout.addWidget(status_box)

        layout.addStretch(1)

    def _btn(self, text, slot):
        b = QPushButton(text)
        b.clicked.connect(slot)
        self._buttons.append(b)
        return b

    def _on_conn_changed(self, connected):
        for b in self._buttons:
            b.setEnabled(connected)

    # ── 程序控制 ───────────────────────────────────────────
    def _on_upload(self):
        if QMessageBox.question(
            self, "上传程序",
            "将把「配置」页的全部参数写入设备并保存到 EEPROM，确认？"
        ) != QMessageBox.Yes:
            return
        try:
            self.config_tab.write_to_device(save=True)
        except ModbusError as e:
            QMessageBox.critical(self, "上传失败", str(e))
            return
        self.ctx.notify("程序（配置）已上传并保存")
        QMessageBox.information(self, "完成", "程序已上传并保存到 EEPROM")

    def _on_retrieve(self):
        try:
            self.config_tab.read_from_device()
        except ModbusError as e:
            QMessageBox.critical(self, "取回失败", str(e))
            return
        self.ctx.notify("程序（配置）已从设备取回，见「配置」页")
        QMessageBox.information(self, "完成", "已取回设备配置，请查看「配置」页")

    def _on_start(self):
        self._write_run(1, "启动")

    def _on_pause(self):
        self._write_run(0, "暂停")

    def _write_run(self, value, action):
        try:
            self.ctx.client.write_single(reg.HR_RUN_ENABLE, value)
        except ModbusError as e:
            QMessageBox.critical(self, "%s失败" % action, str(e))
            return
        self.ctx.notify("已%s采集" % action)
        self._on_refresh_status()

    # ── 配置管理 ───────────────────────────────────────────
    def _on_save(self):
        self._command(reg.CMD_SAVE_CONFIG, "保存配置到 EEPROM", confirm=False)

    def _on_restore(self):
        self._command(reg.CMD_RESTORE_DEFAULTS, "恢复出厂默认并保存",
                       confirm=True,
                       prompt="将清除当前配置并恢复出厂默认，确认？")

    def _on_reset(self):
        self._command(reg.CMD_SOFT_RESET, "软件复位",
                       confirm=True,
                       prompt="设备将软件复位，串口可能短暂断开，确认？")

    def _command(self, cmd, action, confirm, prompt=None):
        if confirm and QMessageBox.question(
            self, action, prompt or ("确认%s？" % action)
        ) != QMessageBox.Yes:
            return
        try:
            self.ctx.client.write_single(reg.HR_COMMAND, cmd)
        except ModbusError as e:
            QMessageBox.critical(self, "%s失败" % action, str(e))
            return
        self.ctx.notify("%s 完成" % action)

    # ── 系统状态 ───────────────────────────────────────────
    def _on_refresh_status(self):
        try:
            regs = self.ctx.client.read_input(reg.IR_SYS_STATUS, 4)
        except ModbusError as e:
            QMessageBox.critical(self, "读取状态失败", str(e))
            return
        status = regs[0]
        err = regs[3]
        self.lbl_run.setText("运行中" if status & reg.SYS_BIT_RUNNING else "已停止")
        self.lbl_ad.setText("正常" if status & reg.SYS_BIT_AD7124_READY else "异常")
        self.lbl_eeprom.setText("正常" if status & reg.SYS_BIT_EEPROM_OK else "异常")
        self.lbl_rtc.setText("正常" if status & reg.SYS_BIT_RTC_OK else "异常")
        self.lbl_sd.setText("正常" if status & reg.SYS_BIT_SD_OK else "未就绪")
        self.lbl_err.setText("0x%04X %s" % (
            err, reg.ERR_CODE_NAMES.get(err, "未知")))
