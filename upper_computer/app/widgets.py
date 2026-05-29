# -*- coding: utf-8 -*-
"""通用 UI 组件：禁滚轮控件 + 说明书弹窗。"""

from PyQt5.QtCore import QSettings
from PyQt5.QtWidgets import (
    QSpinBox, QDoubleSpinBox, QComboBox,
    QDialog, QVBoxLayout, QHBoxLayout, QTextBrowser, QCheckBox, QPushButton,
)


# ── 禁止鼠标滚轮改值的控件 ─────────────────────────────────────────
# 把滚轮事件 ignore，让事件冒泡给父滚动容器（避免鼠标停在控件上时滚动
# 误改数值）。需要时显式把焦点点到控件上，再用键盘上下/方向键修改。

class NoWheelSpinBox(QSpinBox):
    def wheelEvent(self, event):
        event.ignore()


class NoWheelDoubleSpinBox(QDoubleSpinBox):
    def wheelEvent(self, event):
        event.ignore()


class NoWheelComboBox(QComboBox):
    def wheelEvent(self, event):
        event.ignore()


# ── 带"下次不再显示"的说明弹窗 ─────────────────────────────────────

class HelpDialog(QDialog):
    """简易说明弹窗。富文本/HTML 内容，底部有"下次不再弹出"复选框。
    复选框状态用 QSettings 持久化，key 由 settings_key 区分各页面。
    """

    def __init__(self, parent, title, html, settings_key):
        super().__init__(parent)
        self.setWindowTitle(title)
        self._settings_key = settings_key
        self.resize(680, 520)

        layout = QVBoxLayout(self)

        self.browser = QTextBrowser()
        self.browser.setOpenExternalLinks(True)
        self.browser.setHtml(html)
        layout.addWidget(self.browser, 1)

        bottom = QHBoxLayout()
        self.chk_dont_show = QCheckBox("下次进入本页面不再自动弹出")
        bottom.addWidget(self.chk_dont_show)
        bottom.addStretch(1)
        self.btn_close = QPushButton("关闭")
        self.btn_close.clicked.connect(self.accept)
        bottom.addWidget(self.btn_close)
        layout.addLayout(bottom)

    def accept(self):
        QSettings().setValue(self._settings_key, self.chk_dont_show.isChecked())
        super().accept()


def should_auto_show(settings_key) -> bool:
    """根据 QSettings 判断是否应自动弹出说明。
    用户勾选过"不再显示"则返回 False。
    """
    val = QSettings().value(settings_key, False, type=bool)
    return not bool(val)
