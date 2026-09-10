# 2026-07-14 通信协议纠正与数据链路草案提交说明

## 提交主题

```text
纠正通信中断误解并补充数据链路协议草案
```

## 阶段目标

本阶段重新审查 RS422 控制协议和未来 PCIe 图像数据链路，纠正早期把硬件中断概念直接
引入上位机业务协议的问题，并形成后续 ARM、FPGA 和驱动共同评审的协议草案。

当前统一决定先完成上位机基础功能，不立即实现 V1 可靠性机制。草案保留已知问题、风险
和正式联调前的关闭条件，避免临时 V0 协议被误认为产品协议。

## 主要改动

### 1. 删除错误的业务 IRQ 概念

早期临时协议包含：

```text
WAIT_IRQ
OK IRQ count=...
STATUS int=...
```

这些内容没有对应已确认的 ARM 业务含义。现已从源码中删除：

```text
PaProtocol::Command::WaitIrq
PA/FPGA 菜单中的“等待中断”
PaDeviceController 的 IRQ 响应解析和信号
MainWindow 的 IRQ count 状态栏显示
PaDeviceStatus::interruptFlags
STATUS 对 int 字段的强制读取
```

当前 `STATUS` 只解析已有结构化状态字段：

```text
pa / com / rst
wr_state / wr_end
corr_state / corr_end
```

### 2. 明确硬件中断边界

FPGA 的 MSI/MSI-X 中断属于 PCIe 数据面：

```text
FPGA -> MSI/MSI-X -> Linux 驱动 -> read/poll 返回 -> 采集线程
```

该中断用于完成 DMA、唤醒阻塞读取或报告驱动事件，不经过 RS422。ARM 需要主动通知上位机
时，正式协议应使用 `JOB_DONE`、`DEVICE_FAULT`、`EXPOSURE_DONE` 等有业务语义的事件，
不能把 GPIO 或 FPGA 中断直接包装成通用 IRQ 计数。

### 3. 调整自动测试

删除 IRQ 计数和 `interruptFlags` 测试。原来的“主动 IRQ 不结束 PING”改为更符合当前协议
的响应隔离测试：

```text
发送 PING
先注入一条完整但不匹配的 STATUS
确认 STATUS 可以更新结构化状态
确认 PING 仍保持在途
收到 PONG 后才完成 PING
```

同时更新协议命令数量和 STATUS 文本解析样例。

### 4. 新增通信与数据链路协议草案

新增 `doc/communication-data-link-draft.md`，草案版本为 `0.3（问题记录版）`，覆盖：

```text
V0 当前联通测试协议及限制
V0.5 自动心跳过渡思路
V1 事务号、握手、ACK/DONE/ERR/EVT 模型
链路状态和命令/任务状态分离
长任务、错误码和重试边界
控制面与 PCIe 图像 acquisition_id 关联
80 字节可扩展图像帧头讨论方案
采集、预览和原始记录有界队列
链路丢帧、CRC 错误和显示丢帧统计口径
异常恢复、无硬件测试和正式联调顺序
```

事务号已确认可以加入正式协议，由本项目统一确定具体格式。

### 5. 记录协议合理性评审问题

草案将问题按严重、高和中三个级别记录，主要包括：

```text
事务号尚未定义重复请求去重
采集会话生成方和跨链路时序不明确
关键事件可以发现丢失但不能恢复
ACK、DONE 和长任务结束规则不够严格
文本语法、重复字段和 CRC 计算规则未冻结
HELLO 缺少连接随机标识
10 Gbps 光口传输 30 fps RAW12 的带宽余量较小
图像帧头版本、flags、CRC 和时间基准未完全定义
当时的 SEND_IMAGE、START_CORR 和 QUIT 语义不够明确；后续 SEND_IMAGE 已拆为
SEND_SINGLE、START_CONTINUOUS 和 STOP_TRANSFER，QUIT 已从上位机协议和界面移除
```

正式方案建议使用 `host_session_id + seq` 防止重复执行，并由上位机生成
`acquisition_id` 关联控制命令和 PCIe 图像。

### 6. 记录旧 PCIe 参考数据错误

旧 `/home/zhe/pcie/pcie/PROTOCOL.md` 只作为驱动历史参考，其中的 RAW12 数据存在不一致：

```text
3071 * 7714 = 23,689,694 像素
紧密 RAW12 载荷应为 35,534,541 字节
旧文档像素数多 4,020
旧文档载荷长度多 6,030
0x021D0C3B 实际为 35,458,107，也不等于旧文档标注的十进制值
```

旧 RAW12 三字节位序描述也不完整。正式 FPGA 协议必须使用确认后的宽高方向、行填充、
位序和固定像素测试向量，不能直接复制旧文档。

### 7. 明确当前延期决定

当前阶段继续完成上位机基础功能和可替换框架，V0 只用于人工联通和界面流程验证。延期
期间遵守：

```text
不自动重试有副作用的校准和校正命令
不把串口打开等同于正式设备在线
不把界面 FPS 当作 PCIe 零丢帧证明
不固化旧 PCIe 文档中的帧长度和 RAW12 位序
不把 V0 响应超时当作长任务最终执行超时
```

基础功能完成后先关闭协议草案第 16 节的问题，形成正式协议 `1.0`，再进入产品可靠性
联调。

## 文档更新

```text
README.md             删除 IRQ 业务说明并增加协议草案和提交说明索引
doc/architecture.md   明确 RS422 与 PCIe 硬件中断边界
doc/kylin-handover.md 更新测试覆盖描述
设备控制提交说明     增加后续更正并修正 IRQ 相关记录
```

## 验证记录

执行命令：

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build
ctest --output-on-failure
cd ..
git diff --check
xvfb-run -a timeout 2 ./build/pa_host
```

验证结果：

```text
本机 Qt 5 配置和全部目标构建成功
CTest：1/1 测试通过
源码和测试中不存在 WAIT_IRQ、IRQ count 或 interruptFlags 残留
git diff --check：通过
Xvfb 无界面启动正常，2 秒后由 timeout 终止
```

## 当前边界

```text
V1 尚未编译进工程
自动心跳、事务去重、任务查询和事件恢复尚未实现
PCIe 帧头 Draft-A 仍是讨论方案
正式宽高、RAW12 位序、CRC 和光口开销仍待硬件确认
当前 V0 仅用于基础联通，不提供正式可靠性保证
```
