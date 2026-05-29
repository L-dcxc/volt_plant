# 上位机（PyQt5）实现计划

## 目标
为 STM32 植物记录仪开发一个简洁实用的 PyQt5 上位机，通过 Modbus RTU（串口 115200 8N1）与设备通信，覆盖甲方需求的：串口连接、时钟、程序运行控制、通道读取、文件下载（占位）。

## 技术栈
- **PyQt5**（GUI）+ **pymodbus**（Modbus RTU，自动处理 CRC/帧）+ **pyserial**（端口枚举）
- 目标 Python 3.8+，Windows 优先（开发机为 win32）

## 目录结构（新建 `upper_computer/`）
```
upper_computer/
├── README.md               # 安装运行说明、依赖
├── requirements.txt        # PyQt5, pymodbus, pyserial
├── run.py                  # 入口，启动 QApplication
├── app/
│   ├── __init__.py
│   ├── main_window.py      # 主窗口 + QTabWidget 容器
│   ├── modbus_client.py    # Modbus 通信封装（连接/读写/异常处理/32位拆合）
│   ├── registers.py        # 寄存器地址常量 + 编解码（与 modbus_register_map.md 一一对应）
│   └── tabs/
│       ├── __init__.py
│       ├── connection_tab.py   # 串口连接 + 设备信息
│       ├── clock_tab.py        # RTC 读取/设定
│       ├── control_tab.py      # 程序运行控制（上传/取回/启动/暂停/保存/复位）
│       ├── channel_tab.py      # 通道勾选 + 读取实时值
│       ├── config_tab.py       # 全部配置参数编辑（系统 + 16 通道）
│       └── file_tab.py         # 文件下载（界面占位，YMODEM 待固件）
```

## 模块设计

### registers.py — 协议常量层
- 定义所有寄存器地址（保持寄存器 / 输入寄存器）为常量，来源 modbus_register_map.md
- 32 位拆合工具：`split_u32(v)->(hi,lo)`、`join_u32(hi,lo)`、`join_s32`（有符号，VIN/OFFSET/SCALE 用）
- 通道配置基址计算：`ch_base(n)=0x0100+n*0x10`、输入寄存器 `vin_base(n)=n*4`
- 异常码、错误码、文件状态等枚举映射为可读文本

### modbus_client.py — 通信封装层
- 封装 pymodbus `ModbusSerialClient`（method='rtu', 8N1）
- 统一接口：`read_holding(addr,count)`、`read_input(addr,count)`、`write_single(addr,val)`、`write_multiple(addr,values)`
- 异常处理：超时、Modbus 异常响应（0x01-0x04）转为可读错误，向上层抛出
- 连接测试：读 0x0000-0x0003，校验设备 ID / 版本
- 所有读写在工作线程执行，避免阻塞 UI（用 QThread + 信号，或同步+短超时100ms）

### 各标签页

**connection_tab.py（连接）**
- 串口下拉（pyserial 枚举，可刷新）、波特率下拉（默认 115200）、从机地址（默认 1）
- 连接/断开按钮；连接后读设备信息（DEVICE_ID、FW_VERSION、CFG_VERSION）显示
- 连接状态指示，所有其他页依赖此连接

**clock_tab.py（时钟）**
- 读取设备 RTC（输入寄存器 0x004A-0x004D 是最近采样时间；RTC 当前值通过 FC03 0x0030-0x0033）
- "设为系统时间"按钮：用 PC 当前时间 FC16 写 0x0030-0x0033（4 寄存器）
- "手动设定"：日期时间选择控件 → FC16 写入
- 注意：写 RTC_SEC 后固件立即更新

**control_tab.py（程序运行控制）**— 甲方"上传/启动/暂停/取回程序"
- **取回程序**：FC03 批量读全部配置（系统 0x0010-0x0022 + 通道 0x0100-0x01FF），填充到 config_tab
- **上传程序**：把 config_tab 当前值 FC16 批量写入设备，再 FC06 写 0x0040=0x0001 保存到 EEPROM
- **启动程序**：FC06 写 0x0013(RUN_ENABLE)=1
- **暂停运行**：FC06 写 0x0013=0
- **保存配置**：FC06 写 0x0040=0x0001
- **恢复出厂**：FC06 写 0x0040=0x0002（二次确认）
- **软件复位**：FC06 写 0x0040=0x0003（二次确认）
- 显示系统状态（0x0050 SYS_STATUS 各 bit、ERR_CODE）

**channel_tab.py（通道面板）**— 甲方"勾选+读取所选通道"
- 16 个通道复选框（CH0-CH15）
- "读取所选通道"按钮：对勾选通道 FC04 读 VIN（µV）+ RAW
- 表格显示：通道号 / VIN(µV) / VIN(V, 换算) / RAW(hex) / 有效标志
- 读 CH_VALID_MASK(0x0048) 判断数据有效性
- 不做自动轮询（符合甲方"不需实时显示"）

**config_tab.py（配置，全部参数）**
- **系统配置**：Modbus 地址、波特率、采样间隔、记录间隔、平均使能、文件格式、ADC Vref/默认增益/默认滤波
- **通道配置**（16 通道，用表格或可滚动区）：每通道 enable / mode(单端·差分) / 正输入 / 负输入 / sensor_type / gain / filter_mode / range_uv / calib_offset_uv / calib_scale_ppm / warmup_ms
- 增益用下拉（1/2/4/8/16/32/64/128），输入选择用下拉（AIN0-15 / AVSS）
- 本页只编辑内存中的配置模型；实际上传/取回由 control_tab 的按钮触发（也可在本页放快捷上传/取回）
- 32 位字段按 big-endian 高字在前拆分，有符号字段正确处理

**file_tab.py（文件下载，占位）**
- 完整 UI：刷新目录列表（OPEN_DIR→读 FILE_COUNT→逐个 SELECT 读文件名/大小）
- 目录列表逻辑可先实现（依赖固件文件子系统，SD 卡未就绪时会超时/空列表，做好容错提示）
- "下载选中文件"按钮：先置灰或点击提示"YMODEM 传输待固件实现"
- 预留 YMODEM 接收方接口骨架（注释说明后续接入）

## 关键协议细节（来自 modbus_register_map.md + 固件确认）
- CRC：低字节在前（pymodbus 自动处理）
- 32 位值：高字寄存器在低地址（big-endian 分割），寄存器内字节大端
- VIN/OFFSET/SCALE 为有符号 32 位，需 sign-extend
- 设备默认：地址 1，115200，16 通道全使能单端，采样 60s，记录 600s，Vref 2048mV，增益 1
- 写配置流程：FC16 写 → FC06 写 0x0040=0x0001 保存；改地址/波特率需保存+复位
- 响应超时 100ms；帧间静默 ~2ms
- 异常响应：功能码|0x80 + 异常码（pymodbus 自动识别为 ExceptionResponse）

## 实现顺序
1. 脚手架：目录、requirements.txt、run.py、main_window.py（空标签页）、README.md
2. registers.py（协议常量与编解码）
3. modbus_client.py（通信封装 + 连接测试）
4. connection_tab.py（连接 + 设备信息）→ 可先连真机验证
5. channel_tab.py（通道读取）→ 验证 FC04 解码
6. control_tab.py（运行控制）+ config_tab.py（配置编辑）→ 验证 FC03/FC06/FC16
7. clock_tab.py（RTC）
8. file_tab.py（占位 UI）
9. README 补充使用说明

## 验证方式
- 无法在本机 bash 跑 Python（PATH 无 python），由用户在自己环境运行验证
- 代码层面保证：与寄存器映射表逐项对应、异常处理完整、UI 不阻塞
- 提供 requirements.txt 一键装依赖；README 写明 `pip install -r requirements.txt` 与 `python run.py`
- 连接测试作为第一个可验证点（读设备信息）

## 不在本次范围
- YMODEM 实际文件传输（固件未实现，本次只做 UI 占位 + 目录列表逻辑）
- 控制输出配置页（GPIO 电路未定，可后续加；本次 config_tab 暂不含 0x0200 控制输出，或仅留占位）
- 实时自动刷新（甲方明确不需要）
