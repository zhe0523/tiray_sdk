# PA 上位机控制通信与图像数据链路协议草案

文档状态：讨论稿，不代表 ARM、FPGA 或驱动已经实现。

草案版本：0.4（基础上图工作流版）

日期：2026-07-15

当前阶段决策：先完成上位机基础功能和可替换的软件框架，暂不实现 V1 可靠性机制。本文件
继续记录目标设计和已知风险；正式 ARM/FPGA 联调及产品化前，必须重新评审并关闭第 16
节中的协议问题。

## 1. 目的和范围

本文用于统一上位机、ARM、FPGA 和 PCIe 驱动之间的职责及联调口径，重点解决：

```text
如何判断串口打开、设备在线和设备可工作
心跳、状态查询和业务命令如何共存
命令如何匹配响应，迟到响应如何处理
长耗时任务如何报告接受、进度和完成
一次控制操作如何与对应的 PCIe 图像关联
图像帧如何校验、组帧、排队、显示和统计
断线、坏帧、丢帧和设备重启后如何恢复
```

本文不定义探测器算法、校准参数含义和最终 RAW12 打包顺序。相关内容必须以 ARM/FPGA
正式接口和旧软件原始算法为准。

## 2. 总体架构

控制面和数据面必须分开：

```text
控制面：PA Host <-> RS422 <-> ARM
数据面：探测器/光口 -> FPGA -> PCIe -> PA Host
```

控制面负责：

```text
设备识别和协议协商
心跳和状态
校准、校正、采集等命令
任务进度、完成和故障事件
采集会话建立和停止
```

数据面负责：

```text
图像帧传输
帧序号、采集会话号和时间戳
帧长度、像素格式和完整性校验
采集丢帧、协议错误和吞吐统计
```

RS422 不传输图像像素。PCIe 图像线程不直接操作 `MainWindow`、`ImageView` 或控制串口。

## 3. 协议演进阶段

### 3.1 V0：当前联通测试协议

当前实现是无事务号的 ASCII 行协议，命令以 `CRLF` 结束：

```text
PING
STATUS
LOAD_TEMPLATE
MAKE_OFFSET
MAKE_GAIN
CONFIG_TEMPLATE
START_CORR
SEND_SINGLE
START_CONTINUOUS
STOP_TRANSFER
```

响应示例：

```text
OK PONG
OK STATUS int_vector=0x00000000 pa_version=0x00000000 com_version=0x00000000 rst_state=0x00000000 wr_state=0 wr_end=0 corr_state=0 corr_end=0 model=PA-01 serial=SN0001 arm_version=1.0.0 fpga_version=1.0.0
ERR UNKNOWN
```

`model`、`serial`、`arm_version` 和 `fpga_version` 是向后兼容的可选字段，供上位机状态栏
和“关于”界面显示。旧 ARM 程序未返回时，上位机继续接受其余 STATUS 字段，并将对应信息
显示为占位值。字段值不得包含空格；正式 V1 协议冻结时再统一格式和握手返回位置。

该协议可以继续用于前期 RS422 收发和 ARM 命令验证，但存在以下限制：

```text
串口打开后无法确认连接的是正确设备
没有协议版本和设备身份
没有事务号，迟到响应可能影响下一条命令
ACK 和任务最终完成没有区分
所有命令只能共用一套响应超时
没有行长度上限和内容校验约定
```

#### 3.1.1 当前上图命令语义

主界面模式选择只改变上位机本地模式，不向 ARM/FPGA 发送命令。客户必须再次点击开始
按钮确认操作：

```text
Idle + 手动上图        -> SEND_SINGLE
Continuous + 开始上图  -> START_CONTINUOUS
任一上图过程 + 停止上图 -> STOP_TRANSFER
```

`SEND_SINGLE` 要求 FPGA 发送一张图像后停止。`START_CONTINUOUS` 要求 FPGA 自动持续发送
图像，不需要上位机逐帧触发；持续发送直到收到 `STOP_TRANSFER`。该停止命令也用于取消
尚未完成的单帧上图。模式选择、开始和停止必须是三个独立动作，选择 Continuous 不能
直接启动传图。

`STOP_TRANSFER` 是无需响应的即时控制命令。即使开始命令仍在等待响应，上位机也立即取消
该命令的本地等待和超时计时并写出停止命令；写出成功后界面立即恢复 Ready，不等待
ARM/FPGA 回包。不可执行的按钮必须禁用并显示为灰色，不能保留可点击外观。

V0 没有事务号，因此停止后迟到的旧响应可能与紧接着发送的新命令混淆。基础功能阶段接受
这一边界，正式联调前必须通过 V1 事务号、明确的响应匹配和迟到响应处理规则解决。

### 3.2 V0.5：不修改 ARM 的过渡方案

在 ARM 仍使用 V0 协议时，上位机可以先增加自动心跳和链路健康管理，但必须遵守：

```text
只在串口已打开且没有业务命令在途时发送 PING
业务命令执行期间跳过本轮心跳，不建立心跳队列
同一时间仍只允许一条命令在途
PONG 只完成 PING，不完成其他命令
连续多次心跳失败后才判定失联
失联后禁止发送新的业务命令
用户主动断开后不自动重连
```

建议初始参数：

```text
心跳周期             2000 ms
单次心跳响应超时     1000 ms
连续失败阈值         3 次
自动重连初始间隔     2000 ms
自动重连最大间隔    30000 ms
```

这些数值是联调初值，不是最终指标。自动心跳成功只写 `Debug` 日志；首次失败、恢复和最终
离线才写 `Warning/Info`，避免正常心跳快速占满日志。

V0.5 只能降低当前风险，无法彻底解决迟到 `ERR`、普通响应串线和长任务状态不明确的问题。

### 3.3 V1：目标控制协议

目标协议已确认可以增加事务号，由本项目统一确定协议。V1 应增加：

```text
协议版本和设备身份握手
主机生成的事务号 seq
请求、接受、完成、错误和主动事件类型
长任务 job_id
明确的错误码
可选的行级 CRC
最大行长度
```

## 4. RS422 传输约定

以下是建议值，串口物理参数最终由硬件联调确认：

```text
编码             ASCII 可见字符
行结束           CRLF；接收端兼容单 LF
数据位           8
停止位           1
校验位           无
流控             无
默认波特率       115200
最大行长度       1024 字节，包含 CRC 字段，不包含 CRLF
```

接收缓存超过最大行长度仍未遇到换行时，应丢弃到下一行边界并记录协议错误，不能无限增长。

V1 建议使用 `CRC16-CCITT-FALSE`，计算范围为行首到 ` crc=` 之前的全部 ASCII 字节，结果
使用 4 位大写十六进制文本表示。CRC 算法是否启用需要 ARM 性能和现有库确认；一旦启用，
双方必须使用固定测试向量验证，不能各自按描述猜测实现。

示例：

```text
REQ seq=100 cmd=PING crc=XXXX
```

文档中的 `XXXX` 只是占位符，不是该示例的实际计算结果。

## 5. V1 控制消息

### 5.1 消息类型

```text
REQ   上位机发起请求
ACK   ARM 已接收并接受请求
DONE  请求已完成，并携带最终结果
ERR   请求被拒绝或执行失败
EVT   ARM 主动上报的状态变化或事件
```

建议格式：

```text
REQ  seq=<u32> cmd=<name> [key=value ...] crc=<u16>
ACK  seq=<u32> cmd=<name> [job_id=<u32>] crc=<u16>
DONE seq=<u32> cmd=<name> [key=value ...] crc=<u16>
ERR  seq=<u32> cmd=<name> code=<name> [detail=<token>] crc=<u16>
EVT  event_seq=<u32> type=<name> [key=value ...] crc=<u16>
```

字段以单个或多个空格分隔，字段值默认不能包含空格。需要传输自由文本时，应先确定转义
或编码规则；正式协议未确认前不要在 `detail` 中发送任意文本。

### 5.2 事务号

```text
seq 为上位机生成的 uint32 非零整数
同一次请求的 ACK、DONE 和 ERR 必须回显相同 seq 和 cmd
seq 到达 UINT32_MAX 后回绕到 1
连接重新握手后允许从新的随机非零起点开始
未知、重复或已过期 seq 只记录，不得完成当前命令
EVT 不使用 seq，使用独立的 event_seq
```

上位机初期仍可限制为“同一时刻只等待一个 ACK”，降低 ARM 实现复杂度。已经收到 ACK
的长任务转入任务表管理，不应继续阻塞心跳和只读状态命令。

### 5.3 握手和设备识别

串口打开不等于设备在线。建议连接后首先发送：

```text
REQ seq=1 cmd=HELLO host=PA_HOST proto_major=1 proto_minor=0
```

ARM 返回：

```text
DONE seq=1 cmd=HELLO proto_major=1 proto_minor=0 model=<model> serial=<sn> fw=<version> boot_id=<u32> capabilities=<hex>
```

校验规则：

```text
proto_major 不兼容：拒绝进入 Online
proto_minor 不同：按双方能力位选择兼容功能
model 不匹配：提示用户并禁止危险命令
boot_id 变化：认为 ARM 已重启，清空在途事务、任务和旧状态
握手完成后自动读取一次完整 STATUS
```

`serial`、`fw` 和 `capabilities` 的字符范围及最大长度需要 ARM 端确认。

### 5.4 链路状态机

建议上位机链路状态：

```text
Disconnected  串口未打开
Connecting    正在打开串口
Verifying     串口已打开，正在 HELLO/首次 STATUS
Online        设备身份和通信均正常
Degraded      出现暂时超时，但未达到离线阈值
Recovering    已失联，正在按退避策略重连和重新握手
```

命令执行状态单独维护：

```text
Idle          没有等待响应的请求
WaitingAck    请求已发送，等待 ACK/DONE/ERR
Running       长任务已 ACK，通过 job_id 等待事件完成
Failed        最近请求失败，不等于链路断开
```

不得再用单个 `Error` 同时表达串口故障、设备业务错误、响应格式错误和任务失败。

### 5.5 自动心跳

目标行为：

```text
Online 且最近一段时间没有有效收发时触发心跳
WaitingAck 时不插入新的心跳请求
长任务已 ACK 并处于 Running 时仍允许心跳
匹配的 PING 响应清零连续失败计数
业务响应和合法 EVT 可刷新最近接收时间，但不能冒充 PING 的事务响应
单次失败进入 Degraded，连续达到阈值进入 Recovering
恢复后重新 HELLO 和 STATUS，不沿用旧任务状态
```

手动“心跳”入口保留，语义调整为“立即检测”，用于现场诊断。

### 5.6 状态同步和事件

心跳只回答“ARM 通信进程是否有响应”，不能替代业务状态。建议：

```text
握手后读取完整 STATUS
Online 状态每 5 秒低频轮询 STATUS，周期可配置
关键状态变化由 ARM 主动发送 EVT
上位机收到状态事件后更新局部状态，必要时再读取完整 STATUS 校验
```

建议事件：

```text
EVT type=STATUS_CHANGED event_seq=... changed=<mask>
EVT type=JOB_PROGRESS event_seq=... job_id=... progress=<0-100>
EVT type=JOB_DONE event_seq=... job_id=... result=OK
EVT type=JOB_FAILED event_seq=... job_id=... code=...
EVT type=DEVICE_FAULT event_seq=... code=...
EVT type=EXPOSURE_DONE event_seq=... exposure_id=...
```

事件名称必须表达业务含义，不能把 ARM 或 FPGA 的底层硬件中断直接包装成通用 `IRQ`
事件交给上位机。

### 5.7 命令生命周期和重试

命令应按副作用分类：

| 命令 | 类型 | 建议响应 | 自动重试 |
| --- | --- | --- | --- |
| `PING` | 只读 | `DONE` | 可以 |
| `STATUS` | 只读 | `DONE` | 可以 |
| `HELLO` | 只读 | `DONE` | 可以 |
| `LOAD_TEMPLATE` | 有状态操作 | `ACK` 后 `EVT JOB_*` | 默认不重试 |
| `CONFIG_TEMPLATE` | 有状态操作 | `ACK` 或 `DONE` | 默认不重试 |
| `MAKE_OFFSET` | 长任务 | `ACK job_id` 后 `EVT JOB_*` | 禁止盲重试 |
| `MAKE_GAIN` | 长任务 | `ACK job_id` 后 `EVT JOB_*` | 禁止盲重试 |
| `START_CORR` | 有状态操作 | `ACK` 或 `DONE` | 禁止盲重试 |
| `SEND_SINGLE` | 单帧上图 | `DONE` | 禁止盲重试 |
| `START_CONTINUOUS` | 开始持续上图 | `DONE` | 禁止盲重试 |
| `STOP_TRANSFER` | 停止当前单帧或持续上图 | 无 | 可以人工重试，不自动重试 |

长任务至少需要三个时间概念：

```text
发送写入超时       串口写入是否成功
ACK 超时            ARM 是否接收并接受，建议 1～2 秒
任务执行超时       按命令分别配置，可能为数十秒或数分钟
```

任务执行超时后，上位机应先查询任务或设备状态，不能直接重发有副作用的命令。

### 5.8 建议错误码

```text
UNKNOWN_COMMAND
INVALID_ARGUMENT
INVALID_STATE
BUSY
NOT_READY
NOT_SUPPORTED
CRC_ERROR
INTERNAL_ERROR
HARDWARE_FAULT
TIMEOUT
```

错误码用于程序判断，中文说明由上位机映射。不要让上位机依赖 ARM 返回的自由文本判断逻辑。

### 5.9 硬件中断边界

FPGA 通过 PCIe MSI/MSI-X 产生的硬件中断用于通知内核驱动 DMA 完成、设备事件或错误。
驱动处理后唤醒阻塞的 `read/poll`，上位机采集线程读取数据。该过程属于 PCIe 数据面，
不经过 RS422，也不需要上位机发送等待中断命令。

```text
FPGA -> MSI/MSI-X -> Linux 驱动 -> read/poll 返回 -> 采集线程
```

控制协议不定义 `WAIT_IRQ`、`OK IRQ count=...` 或通用 `EVT IRQ`。如果 ARM 内部由 GPIO
中断触发了某项业务变化，ARM 必须把它转换为 `JOB_DONE`、`DEVICE_FAULT`、
`EXPOSURE_DONE` 等语义事件。原始 IRQ 计数最多作为驱动诊断统计，不能作为上位机业务
状态。

## 6. 控制与图像采集的关联

正式采集应引入 `acquisition_id`。建议流程：

```text
1. 上位机完成 HELLO 和 STATUS，确认设备 Online
2. 上位机先启动 PCIe 接收，再生成 acquisition_id
3. 单帧发送 SEND_SINGLE，持续发送 START_CONTINUOUS，并携带 acquisition_id
4. FPGA/PCIe 输出的每帧携带相同 acquisition_id
5. 上位机只接收当前采集的帧，旧采集帧计入 stale_acquisition_frames
6. 需要停止单帧或持续传输时发送 STOP_TRANSFER
7. ARM/FPGA 停止后返回最终帧数和错误统计
```

V0 暂不携带 `acquisition_id`，只验证三条基础命令。V1 冻结时再定义该字段、命令响应与
首帧/末帧的跨链路时序。

## 7. PCIe 图像帧草案

### 7.1 设计原则

```text
DMA 每次读取边界不等于图像帧边界
一帧可能分多次读取，多帧也可能一次读到
帧头必须支持从任意字节位置重新同步
载荷长度必须可校验并设置硬上限
帧序号用于发现传输丢帧，不等同于界面显示帧数
必须明确 DMA 缓冲区所有权和释放时机
```

### 7.2 建议固定头 Draft-A

以下 80 字节小端帧头仅用于推动 FPGA/驱动讨论，字段和偏移尚未确认：

| 偏移 | 大小 | 字段 | 说明 |
| --- | ---: | --- | --- |
| `0x00` | 8 | `magic` | 建议固定 ASCII `TiRayFrm` |
| `0x08` | 2 | `version` | 帧协议版本，建议从 1 开始 |
| `0x0A` | 2 | `header_bytes` | 当前建议 80 |
| `0x0C` | 4 | `flags` | 校正、触发、错误等标志 |
| `0x10` | 8 | `acquisition_id` | 与控制面采集操作对应 |
| `0x18` | 8 | `frame_seq` | 会话内单调递增帧号 |
| `0x20` | 8 | `device_timestamp_us` | 设备时间戳及时间基准待确认 |
| `0x28` | 4 | `width` | 有效像素宽度 |
| `0x2C` | 4 | `height` | 有效像素高度 |
| `0x30` | 2 | `pixel_format` | 像素格式枚举 |
| `0x32` | 2 | `bit_depth` | 有效位数，如 12 或 16 |
| `0x34` | 4 | `row_stride_bytes` | 一行实际字节数，包含填充 |
| `0x38` | 4 | `payload_bytes` | 帧载荷字节数 |
| `0x3C` | 4 | `header_crc32` | 头校验，算法待确认 |
| `0x40` | 4 | `payload_crc32` | 像素载荷校验，算法待确认 |
| `0x44` | 4 | `exposure_us` | 曝光时间；不可用时为 0 |
| `0x48` | 8 | `reserved` | 置 0，供后续兼容扩展 |

接收端必须使用 `header_bytes` 定位载荷，不能假设版本升级后头长度永远为 80。未知主版本
应拒绝，已知版本的扩展字段可按 `header_bytes` 跳过。

建议像素格式枚举：

```text
1  MONO16_LE          每像素 uint16 小端
2  MONO12_IN16_LE     低 12 位有效，uint16 小端
3  MONO12_PACKED      两像素三字节，具体位序待 FPGA 确认
```

前期链路验证优先使用 `MONO16_LE` 或 `MONO12_IN16_LE`，便于抓包和人工检查；只有带宽确实
需要时再启用打包 RAW12。不能在未拿到固定测试向量时根据名称猜测 RAW12 位序。

### 7.3 帧校验和重同步

建议接收顺序：

```text
1. 在字节流中搜索 magic
2. 验证 version、header_bytes 和 header CRC
3. 校验 width、height、stride、payload_bytes 是否在允许范围
4. 等待完整 payload
5. 校验 payload CRC
6. 检查 acquisition_id 和 frame_seq
7. 转换为内部 ImageFrame
8. 将错误和统计上报，继续搜索下一帧
```

所有长度计算必须使用防溢出的 64 位运算，并设置由设备型号决定的最大宽、高、行跨度和
载荷上限。坏头或坏 CRC 不得造成无限缓存或越界读取。

`frame_seq` 回绕、设备重启后清零和 acquisition 切换规则由 FPGA 确认。上位机不能简单使用
无符号减法把乱序或重启误算成巨量丢帧。

## 8. 上位机采集管线

建议数据流：

```text
PCIe 驱动/厂商 SDK
    -> AcquisitionWorker 采集线程
    -> FrameParser 组帧和校验
    -> 共享帧缓冲区/缓冲池
       +-> PreviewQueue 最新帧覆盖 -> FramePresentationController -> ImageSession -> UI
       +-> RecordQueue 有界队列 -> 文件写入（需要保存原始流时）
```

约束：

```text
采集线程不能调用 UI
预览队列只保留最新帧，允许丢弃旧预览帧
记录队列是否允许丢帧必须由产品要求决定
任何队列都必须有容量和内存上限
记录跟不上时应报警或停止采集，不能无限占用内存
DMA 缓冲区归还驱动前，消费者不得继续引用其内存
```

当前 `ImageFrame` 只有图像、来源名、序号和主机接收时间。正式接入时建议增加：

```text
acquisition_id
device_timestamp
pixel_format
frame_flags
integrity_status
```

这些字段属于采集元数据，不应塞进文件名或日志文本再反向解析。

## 9. 统计口径

必须分别统计：

```text
transport_bytes             驱动读取总字节数
transport_errors            驱动/设备节点错误
sync_errors                 帧同步失败次数
header_errors               非法帧头次数
payload_crc_errors          载荷 CRC 错误帧数
protocol_dropped_frames     根据 frame_seq 判断的链路丢帧
stale_acquisition_frames    旧采集操作帧数
acquisition_queue_drops     采集处理队列丢帧
record_queue_drops          原始记录丢帧
preview_dropped_frames      预览层主动覆盖帧数
presented_frames            实际提交给界面的帧数
```

“预览丢帧”不代表 PCIe 丢帧。界面 FPS 也不能作为采集链路完整性的证明。

## 10. 异常恢复

### 10.1 RS422 异常

```text
用户主动断开：关闭自动心跳和自动重连
意外断开：取消等待 ACK 的请求，长任务状态标记未知
达到心跳失败阈值：禁用业务命令并进入 Recovering
重新连接：重新 HELLO、STATUS，不恢复旧 seq 和旧 job_id
ARM boot_id 改变：明确提示 ARM 已重启
```

### 10.2 PCIe 异常

```text
短读：继续累计，不直接判定坏帧
EOF/设备移除：停止采集并报告，不在紧循环中反复读取
坏帧：计数后重同步，不把坏像素提交给 UI
长时间无帧：结合控制面状态判断是正常空闲、采集停止还是数据面故障
停止超时：关闭或取消驱动读取，确保线程可以退出
```

控制面在线但数据面无帧，和数据面有帧但控制面离线，必须显示为不同故障。

## 11. 安全和操作约束

```text
复位、写模板和校准命令需要二次确认或受控流程
有副作用的命令禁止自动盲重试
日志不得记录图像像素和大块二进制载荷
协议错误日志应限制频率，防止故障时耗尽磁盘
诊断信息可以包含端口、版本、统计和最近错误
设备序列号、路径等信息向外发送前需要检查
```

## 12. 无硬件测试方案

FPGA 未准备期间可以先完成：

```text
模拟 ARM：HELLO、心跳、状态、ACK、DONE、ERR 和 EVT
控制测试：心跳跳过 Busy、连续失败、恢复、主动断开不重连
事务测试：迟到、重复、未知和错误 seq 不完成当前请求
任务测试：ACK 后进度、完成、失败和执行超时
字节流生成器：随机拆分帧头和 payload，模拟 DMA 任意分块
坏帧测试：错误 magic、长度、CRC、session 和 frame_seq
压力测试：输入快于预览，确认只丢预览帧且内存有上限
停止测试：无数据阻塞、设备移除和快速停止重启
```

测试必须固定随机种子或保存失败样本，保证问题可以复现。

## 13. 推荐实施顺序

```text
阶段 1  保留 V0 作为临时联通入口，继续完成上位机基础功能和模块边界
阶段 2  在硬件未就绪时使用模拟源验证基础控制、图像显示和错误呈现链路
阶段 3  基础功能稳定后重新评审第 16 节，冻结 V1 控制协议和图像帧协议
阶段 4  实现 V1 模拟 ARM、协议解析器和可靠性自动测试
阶段 5  用命令行工具接收、校验并保存单帧 PCIe 数据
阶段 6  确认固定帧头和 RAW 测试向量，完成连续采集统计
阶段 7  将正式图像源接入 IImageSource 和现有显示链路
阶段 8  完成控制会话与 acquisition_id 联动、异常恢复和长时间测试
```

延期不代表取消。V0 只允许用于基础联通和界面流程验证，不作为正式设备协议；在第 16 节
的严重和高风险问题关闭前，不应基于 V0 承诺命令只执行一次、关键事件必达或图像零错配。

## 14. 待确认清单

### ARM 负责人

```text
HELLO 可以提供哪些型号、序列号、固件和能力字段
PING 正常响应上限和推荐心跳周期
需要主动上报的业务事件及其字段
各命令是立即完成还是长任务
各长任务正常耗时、取消方式和最终状态来源
SEND_SINGLE、START_CONTINUOUS 的响应时点，以及 STOP_TRANSFER 的设备侧停流时序
ARM 重启标识 boot_id 如何生成
是否支持行级 CRC16
```

### FPGA 负责人

```text
图像有效宽高、位深、像素格式和行填充
RAW12 是否打包以及准确位序
帧头、帧号、时间戳、acquisition_id 和 CRC 能否提供
帧号回绕和复位规则
控制命令到首帧、末帧的时序
异常帧或光口错误如何上报
```

### PCIe 驱动负责人

```text
设备节点或 SDK 接口
一次 read/DMA 返回的边界语义
阻塞、超时、poll 和取消方式
缓冲区所有权、对齐和最大传输大小
设备移除和错误码行为
实际持续吞吐和 CPU 占用
```

### 产品/系统负责人

```text
允许的失联检测时间
失联后是否自动重连、停止采集或执行安全动作
预览允许丢帧的范围
原始记录是否必须零丢帧
需要保存哪些采集和设备元数据
危险命令的权限和确认方式
```

## 15. 第一轮评审的最小结论

开始修改正式 ARM/FPGA 协议前，第一轮评审至少要确定：

```text
1. V1 的 ACK/DONE/ERR/EVT 字段和状态转换
2. 心跳周期、超时、失败阈值和失联动作
3. ARM 需要主动上报的业务事件清单
4. 三条上图命令与首帧、末帧的时序
5. 哪些命令属于长任务以及完成条件
6. 图像帧是否携带 acquisition_id、frame_seq、长度和 CRC
7. PCIe 输出使用 MONO16、MONO12_IN16 还是打包 RAW12
8. 原始记录和实时预览的丢帧策略
```

这些结论确定后，再把草案升级为正式协议版本，并分别生成 ARM、FPGA 和上位机固定测试
向量。

## 16. 已知问题与延期决策

本节记录 2026-07-14 协议合理性评审结果。当前统一决定先完成基础功能，再集中完善协议
可靠性。所有问题保留在正式协议待办中，不在当前 V0 代码里提前做不完整实现。

| 优先级 | 已知问题 | 主要风险 | 当前处理决定 |
| --- | --- | --- | --- |
| 严重 | 事务号只匹配响应，尚未定义重复请求去重 | ACK 丢失后重发可能让校准、采集等命令执行两次 | V1 定义 `host_session_id + seq` 去重和结果缓存 |
| 严重 | `acquisition_id` 的跨链路携带和时序尚未冻结 | PCIe 首帧可能早于 RS422 响应，导致图像归属不确定 | 上位机生成 `acquisition_id`，接收就绪后再发启动命令 |
| 严重 | 旧 PCIe 参考文档的像素数、载荷长度和 RAW12 位序存在错误 | FPGA 和上位机可能按不同长度或位序实现 | 旧文档仅作驱动历史参考，正式实现必须使用新测试向量 |
| 高 | `event_seq` 只能发现丢事件，不能恢复关键任务结果 | 丢失 `JOB_DONE` 后上位机无法判断任务是否完成 | V1 增加 `QUERY_JOB`，事件作为通知，查询结果作为事实来源 |
| 高 | `ACK`、`DONE` 和长任务结束规则不够严格 | ARM 与上位机可能对事务何时结束理解不同 | 短命令只返回 `DONE/ERR`；长命令 `ACK job_id` 后转任务事件 |
| 高 | 文本字段、重复键、未知键、大小写和 CRC 规范未冻结 | 双方解析结果或 CRC 计算可能不一致 | V1 给出正式语法、必选字段和固定 CRC 测试向量 |
| 高 | HELLO 示例固定 `seq=1`，缺少连接随机标识 | 重连时旧缓存响应可能错误完成新握手 | V1 使用随机非零 seq 和 64 位 `host_session_id` |
| 高 | V0 停止会取消本地在途命令且不等待响应 | 旧响应迟到后可能错误完成紧接着发送的新命令 | 基础流程接受该边界，V1 使用事务号并定义迟到响应处理 |
| 高 | 10 Gbps 光口承载 30 fps RAW12 的余量较小 | 线路编码和协议开销可能导致持续带宽不足 | 确认编码和开销，完成端到端带宽预算及压力测试 |
| 中 | 80 字节帧头的版本、CRC、flags 和时间基准未完全定义 | 升级、校验和时间关联存在歧义 | FPGA 联调前冻结字段表、计算范围、字节序和测试帧 |
| 中 | `START_CORR` 语义不够明确 | 误操作、状态不一致或与自动恢复冲突 | 正式协议需要明确校正命令 |

### 16.1 旧 PCIe 参考数据更正

`/home/zhe/pcie/pcie/PROTOCOL.md` 当前记录的 `3071 x 7714` RAW12 数据存在不一致：

```text
正确像素数：3071 * 7714 = 23,689,694
正确紧密 RAW12 载荷：23,689,694 * 12 / 8 = 35,534,541 字节
旧文档像素数：23,693,714，相差 4,020
旧文档载荷：35,540,571，相差 6,030
旧文档十六进制 0x021D0C3B 实际为 35,458,107，也不等于其标注的十进制数
```

旧文档对三字节 RAW12 的描述也没有完整表达第二个像素的 12 位数据。正式协议不得引用
这些数值或位序。后续必须先确认 `width/height` 方向、是否逐行填充、奇数行宽处理和固定
像素测试向量，再决定 `MONO12_PACKED`。

### 16.2 基础功能阶段约束

在可靠性协议延期期间，开发遵守以下限制：

```text
V0 命令仅用于人工联通和基础流程验证
不自动重试 SEND_SINGLE、START_CONTINUOUS、MAKE_OFFSET、MAKE_GAIN、START_CORR 等有副作用命令
不把串口打开等同于正式设备在线
不把界面 FPS 当作 PCIe 零丢帧证明
不固化旧 PCIe 参考文档中的帧长度和 RAW12 位序
不把 V0 的响应超时当作长任务最终执行超时
```

基础功能完成后，先解决本节问题并形成协议 `1.0`，再进入正式 ARM/FPGA 联调和产品可靠
性验证。
