# -*- coding: utf-8 -*-
"""文件下载标签页（界面占位）。

目录列举逻辑（OPEN_DIR / SELECT / 读文件名与大小）已实现，可在固件文件
子系统就绪后直接使用。真正的字节流传输走 YMODEM，待固件实现后接入
`_on_download` 中预留的位置。
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QPushButton,
    QListWidget, QListWidgetItem, QLabel, QMessageBox
)

from .. import registers as reg
from ..modbus_client import ModbusError


class FileTab(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        self._buttons = []
        self._file_count = 0
        self._build_ui()
        self.ctx.connectionChanged.connect(self._on_conn_changed)
        self._on_conn_changed(False)

    def _build_ui(self):
        layout = QVBoxLayout(self)

        note = QLabel(
            "说明：文件传输采用 Modbus 协商 + YMODEM 搬运。\n"
            "目录列举已可用；字节流下载待固件 YMODEM 实现后接入。"
        )
        note.setStyleSheet("color: #666;")
        layout.addWidget(note)

        btn_row = QHBoxLayout()
        self.btn_open = self._btn("刷新目录", self._on_open_dir)
        self.btn_download = self._btn("下载选中文件", self._on_download)
        self.btn_download.setEnabled(False)  # 占位：YMODEM 未就绪
        btn_row.addWidget(self.btn_open)
        btn_row.addStretch(1)
        btn_row.addWidget(self.btn_download)
        layout.addLayout(btn_row)

        box = QGroupBox("SD 卡文件")
        v = QVBoxLayout(box)
        self.list_widget = QListWidget()
        self.list_widget.currentRowChanged.connect(self._on_select_row)
        v.addWidget(self.list_widget)
        self.lbl_detail = QLabel("未选择文件")
        v.addWidget(self.lbl_detail)
        layout.addWidget(box, 1)

    def _btn(self, text, slot):
        b = QPushButton(text)
        b.clicked.connect(slot)
        self._buttons.append(b)
        return b

    def _on_conn_changed(self, connected):
        self.btn_open.setEnabled(connected)
        # 下载按钮保持禁用（YMODEM 待实现）
        if not connected:
            self.list_widget.clear()
            self.lbl_detail.setText("未选择文件")

    def _on_open_dir(self):
        c = self.ctx.client
        try:
            c.write_single(reg.HR_FILE_CMD, reg.FILE_CMD_OPEN_DIR)
            state = c.read_input(reg.IR_FILE_XFER_STATE, 2)
            self._file_count = state[1]  # 0x0061 FILE_COUNT
        except ModbusError as e:
            QMessageBox.critical(self, "打开目录失败",
                                 "%s\n（SD 卡未就绪时此功能不可用）" % e)
            return

        self.list_widget.clear()
        if self._file_count == 0:
            self.lbl_detail.setText("目录为空或 SD 卡未就绪")
            return

        for idx in range(self._file_count):
            name = self._select_and_read_name(idx)
            self.list_widget.addItem(QListWidgetItem("%d: %s" % (idx, name)))
        self.ctx.notify("目录读取完成，共 %d 个文件" % self._file_count)

    def _select_and_read_name(self, idx):
        c = self.ctx.client
        try:
            c.write_single(reg.HR_FILE_INDEX, idx)
            c.write_single(reg.HR_FILE_CMD, reg.FILE_CMD_SELECT)
            name_regs = c.read_input(reg.IR_FILE_NAME_BASE, reg.IR_FILE_NAME_REGS)
            return reg.decode_file_name(name_regs)
        except ModbusError:
            return "(读取失败)"

    def _on_select_row(self, row):
        if row < 0:
            self.lbl_detail.setText("未选择文件")
            return
        c = self.ctx.client
        try:
            c.write_single(reg.HR_FILE_INDEX, row)
            c.write_single(reg.HR_FILE_CMD, reg.FILE_CMD_SELECT)
            size_regs = c.read_input(reg.IR_FILE_SIZE_H, 2)
            size = reg.join_u32(size_regs[0], size_regs[1])
            name_regs = c.read_input(reg.IR_FILE_NAME_BASE, reg.IR_FILE_NAME_REGS)
            name = reg.decode_file_name(name_regs)
            self.lbl_detail.setText("已选中：%s，大小 %d 字节" % (name, size))
        except ModbusError as e:
            self.lbl_detail.setText("读取文件信息失败：%s" % e)

    def _on_download(self):
        # 占位：YMODEM 接收待固件实现
        QMessageBox.information(
            self, "暂未实现",
            "YMODEM 文件传输待固件实现后接入。\n"
            "目录列举功能已可用于联调。"
        )
