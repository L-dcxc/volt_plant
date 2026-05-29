# -*- coding: utf-8 -*-
"""通道标签页：勾选通道并读取所选通道的当前测量值（FC04）。

符合甲方需求：不做实时刷新，仅提供选中 + 手动读取。
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QGroupBox,
    QCheckBox, QPushButton, QTableWidget, QTableWidgetItem, QHeaderView,
    QMessageBox, QLabel
)

from .. import registers as reg
from ..modbus_client import ModbusError


class ChannelTab(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        self.checks = []
        self._build_ui()
        self.ctx.connectionChanged.connect(self._on_conn_changed)
        self._on_conn_changed(False)

    def _build_ui(self):
        layout = QVBoxLayout(self)

        sel_box = QGroupBox("选择通道")
        grid = QGridLayout(sel_box)
        for ch in range(reg.CHANNEL_COUNT):
            cb = QCheckBox("CH%d" % ch)
            self.checks.append(cb)
            grid.addWidget(cb, ch // 8, ch % 8)
        layout.addWidget(sel_box)

        btn_row = QHBoxLayout()
        self.btn_all = QPushButton("全选")
        self.btn_none = QPushButton("全不选")
        self.btn_read = QPushButton("读取所选通道")
        self.btn_all.clicked.connect(lambda: self._set_all(True))
        self.btn_none.clicked.connect(lambda: self._set_all(False))
        self.btn_read.clicked.connect(self._on_read)
        btn_row.addWidget(self.btn_all)
        btn_row.addWidget(self.btn_none)
        btn_row.addStretch(1)
        btn_row.addWidget(self.btn_read)
        layout.addLayout(btn_row)

        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(
            ["通道", "电压 (µV)", "电压 (V)", "原始码", "有效"]
        )
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        layout.addWidget(self.table, 1)

        self.hint = QLabel("提示：勾选通道后点击「读取所选通道」。")
        layout.addWidget(self.hint)

    def _set_all(self, state):
        for cb in self.checks:
            cb.setChecked(state)

    def _on_conn_changed(self, connected):
        self.btn_read.setEnabled(connected)

    def _selected_channels(self):
        return [i for i, cb in enumerate(self.checks) if cb.isChecked()]

    def _on_read(self):
        channels = self._selected_channels()
        if not channels:
            QMessageBox.information(self, "提示", "请先勾选至少一个通道")
            return
        try:
            # 读有效标志位
            valid_mask = self.ctx.client.read_input(reg.IR_CH_VALID_MASK, 1)[0]
            rows = []
            for ch in channels:
                base = reg.channel_data_base(ch)
                data = self.ctx.client.read_input(base, 4)
                vin_uv = reg.join_s32(data[reg.IR_CH_VIN_H], data[reg.IR_CH_VIN_L])
                raw = reg.join_u32(data[reg.IR_CH_RAW_H], data[reg.IR_CH_RAW_L])
                valid = bool(valid_mask & (1 << ch))
                rows.append((ch, vin_uv, raw, valid))
        except ModbusError as e:
            QMessageBox.critical(self, "读取失败", str(e))
            return

        self.table.setRowCount(len(rows))
        for r, (ch, vin_uv, raw, valid) in enumerate(rows):
            self.table.setItem(r, 0, QTableWidgetItem("CH%d" % ch))
            self.table.setItem(r, 1, QTableWidgetItem("%d" % vin_uv))
            self.table.setItem(r, 2, QTableWidgetItem("%.6f" % (vin_uv / 1_000_000.0)))
            self.table.setItem(r, 3, QTableWidgetItem("0x%06X" % (raw & 0xFFFFFF)))
            self.table.setItem(r, 4, QTableWidgetItem("是" if valid else "否"))
        self.ctx.notify("已读取 %d 个通道" % len(rows))
