# -*- coding: utf-8 -*-
"""配置标签页：系统配置 + 16 通道完整配置的编辑、读取、上传。

读取/上传由本页方法实现，运行控制页也会调用这些方法
（取回程序 = read_from_device，上传程序 = write_to_device + 保存）。
"""

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QGroupBox,
    QCheckBox, QPushButton, QTableWidget, QTableWidgetItem,
    QHeaderView, QMessageBox, QLabel
)

from .. import registers as reg
from ..modbus_client import ModbusError
from ..widgets import NoWheelSpinBox, NoWheelComboBox, HelpDialog, should_auto_show


_HELP_HTML = """
<h2>配置页说明</h2>

<h3>关键概念</h3>
<ul>
<li><b>通道与AIN固定对应</b>：通道n固定使用AINn（通道0用AIN0，通道1用AIN1，以此类推）。正输入已自动固定，无需手动选择。</li>
<li><b>单端模式</b>：CH 的读数 = V(正输入 AIN) − V(AVSS)。负输入自动设为 <code>AVSS(17)</code>。</li>
<li><b>差分模式</b>：CH 的读数 = V(正输入) − V(负输入)，有符号。<b>硬件限制：差分只能使用相邻的AIN配对</b>（AIN0↔AIN1, AIN2↔AIN3, ...）。</li>
<li><b>差分互斥规则</b>：
  <ul>
    <li>偶数通道（0,2,4...）设为差分时，自动使用下一个AIN作为负输入，并禁用下一个奇数通道。</li>
    <li>奇数通道（1,3,5...）设为差分时，自动使用上一个AIN作为负输入，并禁用上一个偶数通道。</li>
    <li>被禁用的通道整行显示为灰色，不可编辑。</li>
  </ul>
</li>
</ul>

<h3>字段含义</h3>
<table border="1" cellpadding="4" cellspacing="0">
<tr><th>字段</th><th>说明</th></tr>
<tr><td>使能</td><td>勾选则该通道参与采样轮次；未勾选会被序列器跳过。</td></tr>
<tr><td>模式</td><td>单端 / 差分。切换模式时会自动调整负输入并检查互斥关系。</td></tr>
<tr><td>正输入</td><td>已固定为AINn（通道n使用AINn），不可修改。</td></tr>
<tr><td>负输入</td><td>单端模式固定为AVSS(17)；差分模式根据硬件配对规则自动设置，不可手动修改。</td></tr>
<tr><td>增益 (PGA)</td><td><b>硬件模拟放大</b>，把信号实打实地放大后再送 ADC。<br>
满量程 = Vref / 增益。例如 Vref=2.048V、增益=128 时满量程 ±16 mV，<b>用于测量 0–2mV 这类微弱信号</b>。<br>
超量程只会饱和（读数卡在最大/最小码），<b>不会损坏芯片</b>。损坏只发生在 AIN 电压超出供电轨。</td></tr>
<tr><td>量程 (range_uv)</td><td>给主机/上位机看的标度提示（微伏），<b>不参与固件换算</b>。如 2V 档填 2000000，2mV 档填 2000。</td></tr>
<tr><td>偏移 (calib_offset_uv)</td><td>软件标定零点偏移（微伏，有符号）。换算时先扣除。</td></tr>
<tr><td>增益修正 (calib_scale_ppm)</td><td>软件标定增益修正，单位 <b>ppm（百万分之一）</b>。<b>1000000 = ×1.0（不修正）</b>，1000500 = ×1.0005（+500 ppm）。<br>与上面"增益"完全不是一回事：增益是硬件采集前放大，这个是软件采集后修正。<br>最终值：<code>Vcal = (Vraw − offset) × scale / 1000000</code></td></tr>
<tr><td>预热 (warmup_ms)</td><td>采样前等待传感器稳定的毫秒数，0 表示不等待。</td></tr>
</table>

<h3>常见操作流程</h3>
<ol>
<li><b>取回当前配置</b>：点"从设备读取配置"，把设备 EEPROM/RAM 里的值拉下来填进界面。</li>
<li><b>修改</b>：调整需要的字段。注意鼠标滚轮已禁用以防误操作，请用键盘或点击修改。</li>
<li><b>写入（不保存）</b>：仅写入运行时 RAM，复位后丢失。用于临时测试。</li>
<li><b>写入并保存</b>：写入后自动写命令寄存器触发 EEPROM 保存，复位仍有效。</li>
<li>修改 Modbus 地址或波特率后，需要 <b>保存 + 复位</b> 才生效。</li>
</ol>

<h3>差分配置示例</h3>
<ul>
<li><b>场景1</b>：通道0设为差分 → 自动使用AIN0(正)-AIN1(负)，通道1被禁用（灰色）。</li>
<li><b>场景2</b>：通道1设为差分 → 自动使用AIN1(正)-AIN0(负)，通道0被禁用（灰色）。</li>
<li><b>场景3</b>：通道0单端、通道1单端 → 两者独立工作，互不影响。</li>
</ul>

<h3>注意</h3>
<ul>
<li>sensor_type 字段固件内部使用，本页未暴露编辑。</li>
<li>差分配对由硬件电路限制，只能使用相邻的AIN（0↔1, 2↔3, 4↔5, ...）。</li>
</ul>
"""

_HELP_SETTINGS_KEY = "help/config_tab_dont_show"


class ConfigTab(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        self.ch_widgets = []  # 每通道一组控件 dict
        self._help_shown_this_session = False
        self._build_ui()
        self.ctx.connectionChanged.connect(self._on_conn_changed)
        self._on_conn_changed(False)

    def _build_ui(self):
        outer = QVBoxLayout(self)

        # 顶部操作按钮
        btn_row = QHBoxLayout()
        self.btn_read = QPushButton("从设备读取配置")
        self.btn_write = QPushButton("写入设备（不保存）")
        self.btn_write_save = QPushButton("写入并保存到 EEPROM")
        self.btn_help = QPushButton("说明书")
        self.btn_read.clicked.connect(self._on_read)
        self.btn_write.clicked.connect(lambda: self._on_write(save=False))
        self.btn_write_save.clicked.connect(lambda: self._on_write(save=True))
        self.btn_help.clicked.connect(self._show_help)
        btn_row.addWidget(self.btn_read)
        btn_row.addStretch(1)
        btn_row.addWidget(self.btn_write)
        btn_row.addWidget(self.btn_write_save)
        btn_row.addSpacing(20)
        btn_row.addWidget(self.btn_help)
        outer.addLayout(btn_row)

        # 系统配置框（自然高度，不伸展）
        outer.addWidget(self._build_system_box(), 0)
        # 通道配置框（占用所有剩余空间，最大化时能完整显示 16 通道）
        outer.addWidget(self._build_channel_box(), 1)

    def _build_system_box(self):
        box = QGroupBox("系统配置")
        form = QFormLayout(box)

        self.sp_modbus_addr = NoWheelSpinBox()
        self.sp_modbus_addr.setRange(1, 247)
        form.addRow("Modbus 地址：", self.sp_modbus_addr)

        self.cb_baud = NoWheelComboBox()
        self.cb_baud.addItems(["9600", "19200", "38400", "57600", "115200"])
        self.cb_baud.setEditable(True)
        form.addRow("波特率：", self.cb_baud)

        self.sp_sample = NoWheelSpinBox()
        self.sp_sample.setRange(1, 86400)
        self.sp_sample.setSuffix(" 秒")
        form.addRow("采样间隔：", self.sp_sample)

        self.sp_record = NoWheelSpinBox()
        self.sp_record.setRange(1, 86400)
        self.sp_record.setSuffix(" 秒")
        form.addRow("记录间隔：", self.sp_record)

        self.chk_avg = QCheckBox("启用平均")
        form.addRow("平均模式：", self.chk_avg)

        self.cb_format = NoWheelComboBox()
        self.cb_format.addItems(["CSV", "DAT", "TXT"])
        form.addRow("文件格式：", self.cb_format)

        self.sp_vref = NoWheelSpinBox()
        self.sp_vref.setRange(1, 5000)
        self.sp_vref.setSuffix(" mV")
        form.addRow("ADC 参考电压：", self.sp_vref)

        self.cb_def_gain = NoWheelComboBox()
        self.cb_def_gain.addItems([str(g) for g in reg.GAIN_VALUES])
        form.addRow("默认增益：", self.cb_def_gain)

        # 滤波模式由固件固定为 Sinc4 + 10 SPS + 50/60Hz 抑制，不开放编辑。

        return box

    def _build_channel_box(self):
        box = QGroupBox("通道配置（16 通道）")
        v = QVBoxLayout(box)

        cols = ["通道", "使能", "模式", "正输入", "负输入",
                "增益", "量程(µV)", "偏移(µV)", "增益修正(ppm)", "预热(ms)"]
        self.table = QTableWidget(reg.CHANNEL_COUNT, len(cols))
        self.table.setHorizontalHeaderLabels(cols)
        self.table.verticalHeader().setVisible(False)
        hdr = self.table.horizontalHeader()
        hdr.setSectionResizeMode(QHeaderView.ResizeToContents)

        input_choices = ["AIN%d" % i for i in range(16)] + ["AVSS(17)"]

        for ch in range(reg.CHANNEL_COUNT):
            w = {}
            self.table.setItem(ch, 0, QTableWidgetItem("CH%d" % ch))

            w["enable"] = QCheckBox()
            self.table.setCellWidget(ch, 1, self._center(w["enable"]))

            w["mode"] = NoWheelComboBox()
            w["mode"].addItems(["单端", "差分"])
            w["mode"].currentIndexChanged.connect(lambda idx, ch=ch: self._on_mode_changed(ch, idx))
            self.table.setCellWidget(ch, 2, w["mode"])

            w["pos"] = NoWheelComboBox()
            w["pos"].addItems(input_choices)
            w["pos"].setCurrentIndex(ch)
            w["pos"].setEnabled(False)
            self.table.setCellWidget(ch, 3, w["pos"])

            w["neg"] = NoWheelComboBox()
            w["neg"].addItems(input_choices)
            w["neg"].setCurrentIndex(16)
            w["neg"].setEnabled(False)
            self.table.setCellWidget(ch, 4, w["neg"])

            w["gain"] = NoWheelComboBox()
            w["gain"].addItems([str(g) for g in reg.GAIN_VALUES])
            self.table.setCellWidget(ch, 5, w["gain"])

            w["range"] = NoWheelSpinBox()
            w["range"].setRange(0, 2_000_000_000)
            self.table.setCellWidget(ch, 6, w["range"])

            w["offset"] = NoWheelSpinBox()
            w["offset"].setRange(-2_000_000_000, 2_000_000_000)
            self.table.setCellWidget(ch, 7, w["offset"])

            w["scale"] = NoWheelSpinBox()
            w["scale"].setRange(-2_000_000_000, 2_000_000_000)
            self.table.setCellWidget(ch, 8, w["scale"])

            w["warmup"] = NoWheelSpinBox()
            w["warmup"].setRange(0, 2_000_000_000)
            self.table.setCellWidget(ch, 9, w["warmup"])

            self.ch_widgets.append(w)

        v.addWidget(self.table, 1)
        hint = QLabel('说明：通道n固定使用AINn；差分模式下相邻通道互斥（0↔1, 2↔3, ...）。'
                      '点右上角"说明书"了解详细规则。')
        v.addWidget(hint)
        return box

    @staticmethod
    def _center(widget):
        wrap = QWidget()
        lay = QHBoxLayout(wrap)
        lay.addWidget(widget)
        lay.setAlignment(Qt.AlignCenter)
        lay.setContentsMargins(0, 0, 0, 0)
        return wrap

    # ── 说明书 ─────────────────────────────────────────────
    def _show_help(self):
        dlg = HelpDialog(self, "配置页说明", _HELP_HTML, _HELP_SETTINGS_KEY)
        dlg.exec_()

    def showEvent(self, event):
        super().showEvent(event)
        # 本会话仅自动弹出一次（避免标签页来回切换反复弹）；
        # 且用户勾选"下次不再弹出"后永久不再弹。
        if not self._help_shown_this_session and should_auto_show(_HELP_SETTINGS_KEY):
            self._help_shown_this_session = True
            self._show_help()

    def _on_conn_changed(self, connected):
        self.btn_read.setEnabled(connected)
        self.btn_write.setEnabled(connected)
        self.btn_write_save.setEnabled(connected)

    # ── 差分模式互斥逻辑 ────────────────────────────────────
    def _on_mode_changed(self, ch, mode_idx):
        """当通道模式改变时，自动调整负输入并更新互斥通道的状态。

        mode_idx: 0=单端, 1=差分
        """
        w = self.ch_widgets[ch]
        is_differential = (mode_idx == 1)

        if is_differential:
            # 差分模式：根据通道奇偶性设置负输入
            if ch % 2 == 0:
                # 偶数通道：负输入 = AIN(ch+1)
                w["neg"].setCurrentIndex(ch + 1)
            else:
                # 奇数通道：负输入 = AIN(ch-1)
                w["neg"].setCurrentIndex(ch - 1)
        else:
            # 单端模式：负输入 = AVSS(17)
            w["neg"].setCurrentIndex(16)

        # 更新配对通道的使能状态
        self._update_channel_pair_state(ch)

    def _update_channel_pair_state(self, changed_ch):
        """更新与changed_ch配对的通道的使能状态。

        规则：
        - 如果changed_ch是差分模式，禁用其配对通道
        - 如果changed_ch是单端模式，检查配对通道是否也是单端，如果是则启用
        """
        # 找到配对通道
        if changed_ch % 2 == 0:
            pair_ch = changed_ch + 1
        else:
            pair_ch = changed_ch - 1

        # 检查配对通道是否存在
        if pair_ch < 0 or pair_ch >= reg.CHANNEL_COUNT:
            return

        changed_w = self.ch_widgets[changed_ch]
        pair_w = self.ch_widgets[pair_ch]

        changed_is_diff = (changed_w["mode"].currentIndex() == 1)
        pair_is_diff = (pair_w["mode"].currentIndex() == 1)

        # 如果任一通道是差分模式，则禁用另一个通道
        if changed_is_diff or pair_is_diff:
            # 禁用配对通道
            self._set_channel_enabled(pair_ch, False)
        else:
            # 两者都是单端模式，启用配对通道
            self._set_channel_enabled(pair_ch, True)

    def _set_channel_enabled(self, ch, enabled):
        """设置通道的可编辑状态（启用/禁用整行）。"""
        w = self.ch_widgets[ch]

        # 设置所有控件的启用状态
        w["enable"].setEnabled(enabled)
        w["mode"].setEnabled(enabled)
        # pos 和 neg 始终禁用（自动管理）
        w["gain"].setEnabled(enabled)
        w["range"].setEnabled(enabled)
        w["offset"].setEnabled(enabled)
        w["scale"].setEnabled(enabled)
        w["warmup"].setEnabled(enabled)

        # 设置行的视觉效果（灰色背景表示禁用）
        for col in range(self.table.columnCount()):
            item = self.table.item(ch, col)
            if item:
                if enabled:
                    item.setBackground(Qt.white)
                else:
                    item.setBackground(Qt.lightGray)

    def _refresh_all_channel_states(self):
        """刷新所有通道的互斥状态（用于从设备读取配置后）。"""
        # 先重置所有通道为启用状态
        for ch in range(reg.CHANNEL_COUNT):
            self._set_channel_enabled(ch, True)

        # 然后根据差分模式禁用相应的配对通道
        for ch in range(reg.CHANNEL_COUNT):
            w = self.ch_widgets[ch]
            if w["mode"].currentIndex() == 1:  # 差分模式
                # 找到配对通道并禁用
                if ch % 2 == 0:
                    pair_ch = ch + 1
                else:
                    pair_ch = ch - 1

                if 0 <= pair_ch < reg.CHANNEL_COUNT:
                    self._set_channel_enabled(pair_ch, False)

    # ── 增益值 <-> 下拉索引 ─────────────────────────────────
    @staticmethod
    def _gain_to_index(gain):
        try:
            return reg.GAIN_VALUES.index(gain)
        except ValueError:
            return 0

    # ── 读取：从设备拉取系统配置 + 通道配置填入界面 ──────────
    def read_from_device(self):
        c = self.ctx.client
        # 系统配置 0x0010–0x0019（10 个寄存器）
        sysr = c.read_holding(reg.HR_MODBUS_ADDR, 10)
        self.sp_modbus_addr.setValue(sysr[0])
        baud = reg.join_u32(sysr[reg.HR_BAUD_H - reg.HR_MODBUS_ADDR],
                            sysr[reg.HR_BAUD_L - reg.HR_MODBUS_ADDR])
        self.cb_baud.setCurrentText(str(baud))
        self.chk_avg.setChecked(bool(sysr[reg.HR_AVG_ENABLE - reg.HR_MODBUS_ADDR]))
        self.cb_format.setCurrentIndex(
            min(sysr[reg.HR_FILE_FORMAT - reg.HR_MODBUS_ADDR], 2))
        self.sp_sample.setValue(reg.join_u32(
            sysr[reg.HR_SAMPLE_INTERVAL_H - reg.HR_MODBUS_ADDR],
            sysr[reg.HR_SAMPLE_INTERVAL_L - reg.HR_MODBUS_ADDR]))
        self.sp_record.setValue(reg.join_u32(
            sysr[reg.HR_RECORD_INTERVAL_H - reg.HR_MODBUS_ADDR],
            sysr[reg.HR_RECORD_INTERVAL_L - reg.HR_MODBUS_ADDR]))

        # ADC 配置 0x0020–0x0022（仍读取 3 个寄存器，第三个 default_filter
        # 字段在固件中未启用，本页不显示）
        adcr = c.read_holding(reg.HR_ADC_VREF_MV, 3)
        self.sp_vref.setValue(adcr[0])
        self.cb_def_gain.setCurrentIndex(self._gain_to_index(adcr[1]))

        # 通道配置 0x0100 起，每通道读 12 个寄存器
        for ch in range(reg.CHANNEL_COUNT):
            base = reg.channel_base(ch)
            r = c.read_holding(base, reg.CHANNEL_REG_COUNT)
            self._set_channel_widgets(ch, r)

        # 读取完成后刷新所有通道的互斥状态
        self._refresh_all_channel_states()

    def _set_channel_widgets(self, ch, r):
        # filter_mode（CH_OFF_FILTER）字段固件未启用，不刷新到 UI
        w = self.ch_widgets[ch]
        enable, mode = reg.unpack_bytes_hi_lo(r[reg.CH_OFF_FLAGS])
        pos, neg = reg.unpack_bytes_hi_lo(r[reg.CH_OFF_INPUTS])
        _stype, gain = reg.unpack_bytes_hi_lo(r[reg.CH_OFF_SENSOR_GAIN])

        w["enable"].setChecked(bool(enable))
        # 暂时断开信号，避免触发 _on_mode_changed
        w["mode"].blockSignals(True)
        w["mode"].setCurrentIndex(1 if mode == 1 else 0)
        w["mode"].blockSignals(False)

        # 正输入固定为 AINn，负输入根据模式自动设置
        w["pos"].setCurrentIndex(ch)
        if mode == 1:
            # 差分模式：根据硬件配对规则设置负输入
            if ch % 2 == 0:
                w["neg"].setCurrentIndex(ch + 1)
            else:
                w["neg"].setCurrentIndex(ch - 1)
        else:
            # 单端模式：负输入为 AVSS
            w["neg"].setCurrentIndex(16)

        w["gain"].setCurrentIndex(self._gain_to_index(gain))
        w["range"].setValue(reg.join_u32(r[reg.CH_OFF_RANGE_H], r[reg.CH_OFF_RANGE_L]))
        w["offset"].setValue(reg.join_s32(r[reg.CH_OFF_OFFSET_H], r[reg.CH_OFF_OFFSET_L]))
        w["scale"].setValue(reg.join_s32(r[reg.CH_OFF_SCALE_H], r[reg.CH_OFF_SCALE_L]))
        w["warmup"].setValue(reg.join_u32(r[reg.CH_OFF_WARMUP_H], r[reg.CH_OFF_WARMUP_L]))

    @staticmethod
    def _input_to_index(val):
        # 0–15 -> AIN0–15，17(AVSS) -> 索引 16
        if val == reg.ADC_INPUT_AVSS:
            return 16
        return val if 0 <= val <= 15 else 0

    @staticmethod
    def _index_to_input(idx):
        return reg.ADC_INPUT_AVSS if idx == 16 else idx

    # ── 写入：把界面值下发到设备 ────────────────────────────
    def write_to_device(self, save=False):
        c = self.ctx.client

        # 系统配置：分两段写，避开只写的命令寄存器和 RUN_ENABLE。
        # 0x0011–0x0012 波特率（FC16，2 寄存器）
        baud = int(self.cb_baud.currentText())
        c.write_multiple(reg.HR_BAUD_H, list(reg.split_u32(baud)))
        # 0x0010 Modbus 地址（单写）
        c.write_single(reg.HR_MODBUS_ADDR, self.sp_modbus_addr.value())
        # 0x0014 平均、0x0015 文件格式（单写，跳过 0x0013 RUN_ENABLE）
        c.write_single(reg.HR_AVG_ENABLE, 1 if self.chk_avg.isChecked() else 0)
        c.write_single(reg.HR_FILE_FORMAT, self.cb_format.currentIndex())
        # 0x0016–0x0019 采样/记录间隔（FC16，4 寄存器连续）
        interval_regs = list(reg.split_u32(self.sp_sample.value())) + \
            list(reg.split_u32(self.sp_record.value()))
        c.write_multiple(reg.HR_SAMPLE_INTERVAL_H, interval_regs)
        # 0x0020–0x0022 ADC 配置（FC16，3 寄存器）
        # 第 3 个寄存器是 default_filter，固件未启用，写 0 占位
        adc_regs = [
            self.sp_vref.value(),
            reg.GAIN_VALUES[self.cb_def_gain.currentIndex()],
            0,
        ]
        c.write_multiple(reg.HR_ADC_VREF_MV, adc_regs)

        # 通道配置：每通道 12 寄存器，一次 FC16 写入
        for ch in range(reg.CHANNEL_COUNT):
            regs = self._build_channel_regs(ch)
            c.write_multiple(reg.channel_base(ch), regs)

        if save:
            c.write_single(reg.HR_COMMAND, reg.CMD_SAVE_CONFIG)

    def _build_channel_regs(self, ch):
        w = self.ch_widgets[ch]
        enable = 1 if w["enable"].isChecked() else 0
        mode = w["mode"].currentIndex()  # 0 单端 1 差分

        # 正输入固定为 AINn
        pos = ch

        # 负输入根据模式自动设置
        if mode == 1:
            # 差分模式：根据硬件配对规则
            if ch % 2 == 0:
                neg = ch + 1
            else:
                neg = ch - 1
        else:
            # 单端模式：AVSS
            neg = reg.ADC_INPUT_AVSS

        gain = reg.GAIN_VALUES[w["gain"].currentIndex()]
        range_uv = w["range"].value()
        offset = w["offset"].value()
        scale = w["scale"].value()
        warmup = w["warmup"].value()

        regs = [0] * reg.CHANNEL_REG_COUNT
        regs[reg.CH_OFF_FLAGS] = reg.pack_bytes_hi_lo(enable, mode)
        regs[reg.CH_OFF_INPUTS] = reg.pack_bytes_hi_lo(pos, neg)
        # sensor_type 高字节保留 0（固件默认 255 仅内部用，写 0 不影响换算）
        regs[reg.CH_OFF_SENSOR_GAIN] = reg.pack_bytes_hi_lo(0, gain)
        # CH_OFF_FILTER 保留 0（固件未启用此字段）
        regs[reg.CH_OFF_RANGE_H], regs[reg.CH_OFF_RANGE_L] = reg.split_u32(range_uv)
        regs[reg.CH_OFF_OFFSET_H], regs[reg.CH_OFF_OFFSET_L] = reg.split_s32(offset)
        regs[reg.CH_OFF_SCALE_H], regs[reg.CH_OFF_SCALE_L] = reg.split_s32(scale)
        regs[reg.CH_OFF_WARMUP_H], regs[reg.CH_OFF_WARMUP_L] = reg.split_u32(warmup)
        return regs

    # ── 按钮回调 ───────────────────────────────────────────
    def _on_read(self):
        try:
            self.read_from_device()
        except ModbusError as e:
            QMessageBox.critical(self, "读取失败", str(e))
            return
        self.ctx.notify("配置已从设备读取")

    def _on_write(self, save):
        try:
            self.write_to_device(save=save)
        except ModbusError as e:
            QMessageBox.critical(self, "写入失败", str(e))
            return
        msg = "配置已写入并保存到 EEPROM" if save else "配置已写入设备（未保存）"
        self.ctx.notify(msg)
        if save:
            QMessageBox.information(self, "完成", msg)
