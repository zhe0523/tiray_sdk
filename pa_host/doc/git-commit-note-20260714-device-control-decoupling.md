# 2026-07-14 设备控制链路解耦提交说明

> 后续更正：早期实现把未确认的硬件中断概念引入了 RS422 业务协议，产生了
> `WAIT_IRQ`、`OK IRQ count=...` 和 `STATUS int=...`。这些内容已在后续修改中删除。
> FPGA 的 MSI/MSI-X 中断只属于 PCIe 驱动内部，ARM 主动通知必须使用有业务语义的事件。

## 提交主题

```text
解耦 RS422 设备控制与界面状态
```

## 阶段目标

本阶段不继续扩展本地回放，也不预设尚未确认的 PCIe 图像协议。重点整理实际产品会使用
的 RS422 控制链路，把串口传输、ASCII 协议、命令生命周期和界面状态分离，并在没有
ARM/FPGA 硬件时通过模拟传输完成自动测试。

改造前，`MainWindow` 直接连接 `SerialClient`，负责发送命令、记录文本行、解析
`PaProtocol::Response` 和更新状态栏。该结构难以测试命令超时、重复发送和链路故障，
也会让后续协议调整持续影响界面代码。

## 主要改动

### 1. 新增 ILineTransport

新增 `ILineTransport` 作为 ASCII 行传输边界，接口只包含：

```text
打开端口
关闭端口
查询连接状态和端口名
发送完整文本行
接收完整文本行
报告连接变化和传输错误
```

该接口不暴露 `QSerialPort`。正式运行由 `SerialClient` 实现，自动测试使用内存模拟
实现，因此测试设备控制逻辑不需要串口设备。

### 2. 调整 SerialClient

`SerialClient` 改为实现 `ILineTransport`，仍只负责：

```text
Linux/Windows 串口打开和关闭
115200 等波特率及 8N1 参数配置
发送带 CRLF 的 UTF-8 命令行
缓存不完整接收数据并按换行切分
转发 QSerialPort 错误
```

串口类不解释 `OK`、`ERR` 或 `STATUS` 的业务含义。

### 3. 新增 PaDeviceController

新增设备控制器，位于 `MainWindow`、`PaProtocol` 和 `ILineTransport` 之间：

```text
MainWindow
    -> PaDeviceController
        -> PaProtocol
        -> ILineTransport
            -> SerialClient
```

控制器提供四种状态：

```text
Disconnected  未连接
Ready         已连接且可以发送命令
Busy          已发送命令，正在等待响应
Error         响应失败、超时或传输错误
```

### 4. 管理单条在途命令

当前 ASCII 协议没有命令序号，因此同一时间只允许一条命令在途。控制器负责：

```text
拒绝未连接时发送
拒绝上一条命令未完成时重复发送
写入成功后启动响应定时器
收到 OK/ERR 后结束对应命令
断开连接时取消在途命令
传输错误时立即结束在途命令
错误状态下串口仍打开时允许直接重试
```

默认响应超时为 5000 ms，可通过 `setCommandTimeoutMs()` 调整。该值表示命令确认响应
超时，不代表 Offset、Gain 或校正算法的完整执行时间。

### 5. 防止迟到响应串线

命令超时后，旧响应可能晚于下一条命令到达。控制器对以下响应进行严格匹配：

```text
PING      -> PONG
STATUS    -> STATUS
```

等待 `PING` 时收到迟到的 `STATUS`，或等待 `STATUS` 时收到迟到的 `PONG`，都不会错误完成
当前命令。不匹配的响应仍会进入 RX 日志，并保留当前在途命令直到正确响应或超时。

### 6. 结构化设备状态

`OK STATUS` 不再由主窗口直接读取字符串键值。控制器校验并转换：

```text
pa / com / rst
wr_state / wr_end
corr_state / corr_end
```

字段缺失或数值无效时不会发出有效状态，并将当前 `STATUS` 命令标记为失败。

### 7. 收敛 MainWindow

`MainWindow` 现在只负责：

```text
读取端口和波特率控件
调用连接、断开和发送命令
显示 TX/RX、命令完成和错误日志
显示结构化 STATUS
根据设备状态控制菜单和按钮
```

未连接时 PA/FPGA 菜单和顶部设备命令不可用；命令执行期间禁止重复操作。连接后端口、
波特率和刷新端口不可修改，断开后恢复。

### 8. 增加模拟传输测试

新增 `FakeLineTransport`，测试覆盖：

```text
未连接发送被拒绝
模拟串口连接和参数传递
单条在途命令及重复发送拦截
完整 STATUS 数值解析
迟到 STATUS 不结束 PING
迟到 PONG 不结束 STATUS
命令响应超时
错误状态下重试并恢复 Ready
在途命令遇到传输故障立即失败
串口打开失败
```

## 文档更新

```text
README.md             增加控制器、传输接口、运行规则和测试项
doc/architecture.md   更新 RS422 依赖方向、状态机和职责边界
doc/kylin-handover.md 增加 Kylin 状态栏、按钮禁用和超时检查项
```

## 验证记录

执行命令：

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build
ctest --output-on-failure
```

验证结果：

```text
本机 Qt 5 配置和生成成功
pa_core、pa_transport、pa_host、pa_host_tests、pa_image_benchmark 编译成功
CTest：1/1 测试通过
git diff --check：通过
Xvfb 无界面启动烟测正常，2 秒后由 timeout 终止
```

## 当前边界

```text
当前控制协议仍是单请求、单响应的 ASCII 行协议
默认 5 秒响应超时需要 ARM 实机联调确认
STATUS 字段按当前 README 中的完整响应格式校验
图像数据仍走未来光口/PCIe 链路，不进入 SerialClient
PCIe 帧头、DMA 缓冲区和驱动接口仍等待 FPGA/硬件条件
```
