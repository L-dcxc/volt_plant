# -*- coding: utf-8 -*-
"""主窗口：组装标签页，持有共享的 Modbus 连接和配置模型。"""

from PyQt5.QtCore import QObject, pyqtSignal
from PyQt5.QtWidgets import (
    QMainWindow, QTabWidget, QWidget, QVBoxLayout, QLabel, QStatusBar
)

from .modbus_client import ModbusClient
from .tabs.connection_tab import ConnectionTab
from .tabs.clock_tab import ClockTab
from .tabs.control_tab import ControlTab
from .tabs.channel_tab import ChannelTab
from .tabs.config_tab import ConfigTab
from .tabs.file_tab import FileTab


class AppContext(QObject):
    """各标签页共享的应用上下文。

    持有唯一的 ModbusClient，并在连接状态变化时广播信号，
    让各页据此启用/禁用控件。
    """

    connectionChanged = pyqtSignal(bool)   # 参数：是否已连接
    statusMessage = pyqtSignal(str)         # 状态栏消息

    def __init__(self):
        super().__init__()
        self.client = ModbusClient()

    def set_connected(self, connected):
        self.connectionChanged.emit(connected)

    def notify(self, message):
        self.statusMessage.emit(message)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("植物记录仪上位机")
        self.resize(900, 640)

        self.ctx = AppContext()

        self.tabs = QTabWidget()
        self.setCentralWidget(self.tabs)

        # 配置页先建（control 页需要引用它做上传/取回）
        self.config_tab = ConfigTab(self.ctx)
        self.connection_tab = ConnectionTab(self.ctx)
        self.control_tab = ControlTab(self.ctx, self.config_tab)
        self.channel_tab = ChannelTab(self.ctx)
        self.clock_tab = ClockTab(self.ctx)
        self.file_tab = FileTab(self.ctx)

        self.tabs.addTab(self.connection_tab, "连接")
        self.tabs.addTab(self.clock_tab, "时钟")
        self.tabs.addTab(self.control_tab, "运行控制")
        self.tabs.addTab(self.channel_tab, "通道")
        self.tabs.addTab(self.config_tab, "配置")
        self.tabs.addTab(self.file_tab, "文件下载")

        self.setStatusBar(QStatusBar())
        self.ctx.statusMessage.connect(self._on_status)
        self.ctx.connectionChanged.connect(self._on_conn_changed)
        self._on_conn_changed(False)

    def _on_status(self, msg):
        self.statusBar().showMessage(msg, 8000)

    def _on_conn_changed(self, connected):
        state = "已连接" if connected else "未连接"
        self.statusBar().showMessage("设备%s" % state, 4000)

    def closeEvent(self, event):
        self.ctx.client.disconnect()
        super().closeEvent(event)
