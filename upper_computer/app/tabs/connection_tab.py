# -*- coding: utf-8 -*-
"""连接标签页：串口选择、连接/断开、设备信息显示。"""

import serial.tools.list_ports
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QFormLayout, QHBoxLayout, QGroupBox,
    QComboBox, QPushButton, QLabel, QMessageBox
)

from .. import registers as reg
from ..modbus_client import ModbusError

BAUD_CHOICES = ["9600", "19200", "38400", "57600", "115200"]


class ConnectionTab(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        self._build_ui()
        self._refresh_ports()

    def _build_ui(self):
        layout = QVBoxLayout(self)

        conn_box = QGroupBox("串口连接")
        form = QFormLayout(conn_box)

        port_row = QHBoxLayout()
        self.port_combo = QComboBox()
        self.refresh_btn = QPushButton("刷新")
        self.refresh_btn.clicked.connect(self._refresh_ports)
        port_row.addWidget(self.port_combo, 1)
        port_row.addWidget(self.refresh_btn)
        form.addRow("串口号：", port_row)

        self.baud_combo = QComboBox()
        self.baud_combo.addItems(BAUD_CHOICES)
        self.baud_combo.setCurrentText("115200")
        form.addRow("波特率：", self.baud_combo)

        self.addr_combo = QComboBox()
        self.addr_combo.addItems([str(i) for i in range(1, 248)])
        self.addr_combo.setCurrentText("1")
        form.addRow("从机地址：", self.addr_combo)

        btn_row = QHBoxLayout()
        self.connect_btn = QPushButton("连接")
        self.connect_btn.clicked.connect(self._on_connect)
        self.disconnect_btn = QPushButton("断开")
        self.disconnect_btn.clicked.connect(self._on_disconnect)
        self.disconnect_btn.setEnabled(False)
        btn_row.addWidget(self.connect_btn)
        btn_row.addWidget(self.disconnect_btn)
        form.addRow(btn_row)

        layout.addWidget(conn_box)

        info_box = QGroupBox("设备信息")
        info_form = QFormLayout(info_box)
        self.lbl_status = QLabel("未连接")
        self.lbl_device_id = QLabel("-")
        self.lbl_fw = QLabel("-")
        self.lbl_cfg = QLabel("-")
        info_form.addRow("状态：", self.lbl_status)
        info_form.addRow("设备 ID：", self.lbl_device_id)
        info_form.addRow("固件版本：", self.lbl_fw)
        info_form.addRow("配置版本：", self.lbl_cfg)
        layout.addWidget(info_box)

        layout.addStretch(1)

    def _refresh_ports(self):
        self.port_combo.clear()
        ports = serial.tools.list_ports.comports()
        for p in ports:
            self.port_combo.addItem("%s - %s" % (p.device, p.description), p.device)
        if not ports:
            self.port_combo.addItem("（未找到串口）", None)

    def _selected_port(self):
        return self.port_combo.currentData()

    def _on_connect(self):
        port = self._selected_port()
        if not port:
            QMessageBox.warning(self, "提示", "请选择一个有效串口")
            return
        baud = int(self.baud_combo.currentText())
        addr = int(self.addr_combo.currentText())
        try:
            self.ctx.client.connect(port, baudrate=baud, slave=addr)
            info = self.ctx.client.read_device_info()
        except ModbusError as e:
            self.ctx.client.disconnect()
            QMessageBox.critical(self, "连接失败", str(e))
            self.ctx.set_connected(False)
            return

        self.lbl_status.setText("已连接 @ %s" % port)
        self.lbl_device_id.setText("0x%08X" % info["device_id"])
        self.lbl_fw.setText("0x%04X" % info["fw_version"])
        self.lbl_cfg.setText("0x%04X" % info["cfg_version"])
        self.connect_btn.setEnabled(False)
        self.disconnect_btn.setEnabled(True)
        self.port_combo.setEnabled(False)
        self.baud_combo.setEnabled(False)
        self.addr_combo.setEnabled(False)
        self.ctx.set_connected(True)
        self.ctx.notify("连接成功，设备 ID 0x%08X" % info["device_id"])

    def _on_disconnect(self):
        self.ctx.client.disconnect()
        self.lbl_status.setText("未连接")
        self.lbl_device_id.setText("-")
        self.lbl_fw.setText("-")
        self.lbl_cfg.setText("-")
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)
        self.port_combo.setEnabled(True)
        self.baud_combo.setEnabled(True)
        self.addr_combo.setEnabled(True)
        self.ctx.set_connected(False)
        self.ctx.notify("已断开")
