# -*- coding: utf-8 -*-
"""文件下载标签页。

目录列举走 Modbus 寄存器协商。下载文件时：先发 FILE_CMD=START → 等
固件 FC06 应答完成 → USART1 切到 YMODEM-1K → 上位机用 pyserial 直接
跑 YMODEM 接收，完成后再回查 IR_FILE_XFER_STATE 确认。

支持多选：下载多个文件时选目标文件夹批量保存；删除多个文件时按索引
从大到小删（固件每次删除后会重新快照目录，降序删可保证剩余索引有效）。
"""

import os
from time import sleep

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QAbstractItemView, QApplication, QFileDialog, QGroupBox, QHBoxLayout,
    QLabel, QListWidget, QListWidgetItem, QMessageBox, QProgressDialog,
    QPushButton, QVBoxLayout, QWidget,
)

from .. import registers as reg
from ..modbus_client import ModbusError
from .. import ymodem_receiver


DOWNLOAD_RX_TIMEOUT_S = 0.5
DEVICE_RECOVER_S = 0.2  # let the device flip back to Modbus after a transfer


class FileTab(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        self._buttons = []
        self._file_count = 0
        self._files = []        # cache: row -> (name, size)
        self._connected = False
        self._build_ui()
        self.ctx.connectionChanged.connect(self._on_conn_changed)
        self._on_conn_changed(False)

    def _build_ui(self):
        layout = QVBoxLayout(self)

        note = QLabel(
            "说明：文件传输采用 Modbus 协商 + YMODEM-1K 搬运。\n"
            "点「刷新目录」加载列表；可按住 Ctrl/Shift 多选，再批量下载或删除。"
        )
        note.setStyleSheet("color: #666;")
        layout.addWidget(note)

        btn_row = QHBoxLayout()
        self.btn_open = self._btn("刷新目录", self._on_open_dir)
        self.btn_download = self._btn("下载选中文件", self._on_download)
        self.btn_delete = self._btn("删除选中文件", self._on_delete)
        self.btn_download.setEnabled(False)
        self.btn_delete.setEnabled(False)
        self.btn_delete.setStyleSheet("color: #b00;")
        btn_row.addWidget(self.btn_open)
        btn_row.addStretch(1)
        btn_row.addWidget(self.btn_download)
        btn_row.addWidget(self.btn_delete)
        layout.addLayout(btn_row)

        box = QGroupBox("SD 卡文件")
        v = QVBoxLayout(box)
        self.lbl_capacity = QLabel("容量：未知")
        self.lbl_capacity.setStyleSheet("color: #444;")
        v.addWidget(self.lbl_capacity)
        self.list_widget = QListWidget()
        self.list_widget.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.list_widget.itemSelectionChanged.connect(self._on_selection_changed)
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
        self._connected = bool(connected)
        self.btn_open.setEnabled(connected)
        if not connected:
            self.list_widget.clear()
            self._files = []
            self.lbl_detail.setText("未选择文件")
            self.lbl_capacity.setText("容量：未知")
        self._on_selection_changed()

    def _refresh_capacity(self):
        try:
            regs = self.ctx.client.read_input(reg.IR_SD_TOTAL_MB_H, 4)
        except ModbusError:
            self.lbl_capacity.setText("容量：读取失败")
            return
        total_mb = reg.join_u32(regs[0], regs[1])
        free_mb = reg.join_u32(regs[2], regs[3])
        if total_mb == 0:
            self.lbl_capacity.setText("容量：未知（SD 卡未就绪）")
            return
        used_mb = max(total_mb - free_mb, 0)
        pct = (used_mb * 100) // total_mb if total_mb else 0
        self.lbl_capacity.setText(
            "容量：已用 %d MB / 共 %d MB（%d%%），剩余 %d MB"
            % (used_mb, total_mb, pct, free_mb))

    def _selected_rows(self):
        return sorted({self.list_widget.row(it)
                       for it in self.list_widget.selectedItems()})

    def _on_selection_changed(self):
        rows = self._selected_rows()
        enabled = bool(self._connected and rows)
        self.btn_download.setEnabled(enabled)
        self.btn_delete.setEnabled(enabled)

        if not rows:
            self.lbl_detail.setText("未选择文件")
        elif len(rows) == 1:
            name, size = self._files[rows[0]]
            self.lbl_detail.setText("已选中：%s，大小 %d 字节" % (name, size))
        else:
            total = sum(self._files[r][1] for r in rows)
            self.lbl_detail.setText("已选中 %d 个文件，共 %d 字节" % (len(rows), total))

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
        self._files = []
        self._refresh_capacity()

        if self._file_count == 0:
            self.lbl_detail.setText("目录为空或 SD 卡未就绪")
            self._on_selection_changed()
            return

        for idx in range(self._file_count):
            name, size = self._select_and_read_meta(idx)
            self._files.append((name, size))
            self.list_widget.addItem(
                QListWidgetItem("%d: %s  (%d 字节)" % (idx, name, size)))
        self.ctx.notify("目录读取完成，共 %d 个文件" % self._file_count)
        self._on_selection_changed()

    def _select_and_read_meta(self, idx):
        c = self.ctx.client
        try:
            c.write_single(reg.HR_FILE_INDEX, idx)
            c.write_single(reg.HR_FILE_CMD, reg.FILE_CMD_SELECT)
            size_regs = c.read_input(reg.IR_FILE_SIZE_H, 2)
            size = reg.join_u32(size_regs[0], size_regs[1])
            name_regs = c.read_input(reg.IR_FILE_NAME_BASE, reg.IR_FILE_NAME_REGS)
            name = reg.decode_file_name(name_regs)
            return name, size
        except ModbusError:
            return "(读取失败)", 0

    # ── 删除 ───────────────────────────────────────────────
    def _on_delete(self):
        rows = self._selected_rows()
        if not rows:
            return
        names = [self._files[r][0] for r in rows]

        if len(rows) == 1:
            prompt = "确认删除设备上的「%s」？此操作不可撤销。" % names[0]
        else:
            preview = "\n".join(names[:8])
            if len(names) > 8:
                preview += "\n…"
            prompt = ("确认删除选中的 %d 个文件？此操作不可撤销。\n\n%s"
                      % (len(rows), preview))

        if QMessageBox.question(
            self, "删除文件", prompt,
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No,
        ) != QMessageBox.Yes:
            return

        c = self.ctx.client
        failed = []
        # Delete highest index first: the device re-snapshots the directory
        # after each delete, so lower indices stay valid only if we go down.
        for r in sorted(rows, reverse=True):
            name = self._files[r][0]
            try:
                c.write_single(reg.HR_FILE_INDEX, r)
                c.write_single(reg.HR_FILE_CMD, reg.FILE_CMD_SELECT)
                c.write_single(reg.HR_FILE_CMD, reg.FILE_CMD_DELETE)
            except ModbusError as e:
                failed.append((name, str(e)))

        done = len(rows) - len(failed)
        self.ctx.notify("已删除 %d 个文件" % done)
        self._on_open_dir()  # indices shifted; reload

        if failed:
            detail = "\n".join("%s：%s" % (n, e) for n, e in failed)
            QMessageBox.critical(self, "部分删除失败",
                                 "以下文件删除失败：\n%s" % detail)

    # ── 下载 ───────────────────────────────────────────────
    def _on_download(self):
        rows = self._selected_rows()
        if not rows:
            return

        c = self.ctx.client
        ser = getattr(c._client, "socket", None)
        if ser is None:
            QMessageBox.critical(self, "下载失败", "串口未就绪")
            return

        # Resolve (row, name, size, save_path) for every selected file.
        if len(rows) == 1:
            name, size = self._files[rows[0]]
            save_path, _ = QFileDialog.getSaveFileName(
                self, "保存为", name, "All Files (*)")
            if not save_path:
                return
            targets = [(rows[0], name, size, save_path)]
        else:
            folder = QFileDialog.getExistingDirectory(self, "选择保存文件夹")
            if not folder:
                return
            targets = [(r, self._files[r][0], self._files[r][1],
                        os.path.join(folder, self._files[r][0])) for r in rows]

        grand_total = max(sum(t[2] for t in targets), 1)
        progress = QProgressDialog("准备下载...", "取消", 0, grand_total, self)
        progress.setWindowModality(Qt.WindowModal)
        progress.setMinimumDuration(0)
        progress.setValue(0)
        QApplication.processEvents()

        prev_timeout = ser.timeout
        ser.timeout = DOWNLOAD_RX_TIMEOUT_S

        done_bytes = 0
        ok_count = 0
        failed = []
        canceled = False

        try:
            for i, (row, name, size, path) in enumerate(targets):
                if progress.wasCanceled():
                    canceled = True
                    break

                base = done_bytes
                progress.setLabelText(
                    "正在下载 (%d/%d) %s" % (i + 1, len(targets), name))
                QApplication.processEvents()

                try:
                    fp = open(path, "wb")
                except OSError as e:
                    failed.append((name, "无法保存：%s" % e))
                    done_bytes += size
                    continue

                try:
                    ser.reset_input_buffer()
                except Exception:
                    pass

                # START hands USART1 to YMODEM after the FC06 ACK is sent.
                try:
                    c.write_single(reg.HR_FILE_INDEX, row)
                    c.write_single(reg.HR_FILE_CMD, reg.FILE_CMD_START)
                except ModbusError as e:
                    fp.close()
                    self._safe_remove(path)
                    failed.append((name, "启动失败：%s" % e))
                    done_bytes += size
                    continue

                try:
                    with fp:
                        def on_prog(rx, total, _base=base):
                            progress.setValue(min(_base + rx, grand_total))
                            QApplication.processEvents()

                        def is_canceled():
                            QApplication.processEvents()
                            return progress.wasCanceled()

                        _fn, sz = ymodem_receiver.receive_file(
                            ser, fp, on_progress=on_prog, is_canceled=is_canceled)
                    ok_count += 1
                except ymodem_receiver.YModemCanceled:
                    self._safe_remove(path)
                    canceled = True
                    self._recover_modbus(ser)
                    break
                except ymodem_receiver.YModemError as e:
                    self._safe_remove(path)
                    failed.append((name, str(e)))

                done_bytes = base + size
                progress.setValue(min(done_bytes, grand_total))
                self._recover_modbus(ser)
        finally:
            ser.timeout = prev_timeout
            progress.close()

        self._report_download(len(targets), ok_count, failed, canceled)

    def _safe_remove(self, path):
        try:
            os.remove(path)
        except OSError:
            pass

    def _recover_modbus(self, ser):
        """Give the device a beat to switch back to Modbus, then flush RX."""
        sleep(DEVICE_RECOVER_S)
        try:
            ser.reset_input_buffer()
        except Exception:
            pass

    def _report_download(self, total, ok_count, failed, canceled):
        if canceled:
            QMessageBox.warning(
                self, "已取消",
                "已取消下载。成功 %d 个，剩余未完成。" % ok_count)
        elif failed:
            detail = "\n".join("%s：%s" % (n, e) for n, e in failed)
            QMessageBox.critical(
                self, "部分下载失败",
                "成功 %d 个，失败 %d 个：\n%s" % (ok_count, len(failed), detail))
        else:
            QMessageBox.information(
                self, "下载完成", "全部 %d 个文件下载完成。" % total)
