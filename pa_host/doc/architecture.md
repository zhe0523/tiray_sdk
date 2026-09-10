# PA Host 架构与扩展边界

当前阶段优先保证控制链路、图像链路和界面工作流可以独立演进。旧软件算法尚未拿到时，内置算法只作为可运行的默认实现，不应成为 UI 的直接依赖。

## 模块划分

```text
pa_host（Qt 界面）
  MainWindow（模块组装与工作流）
        |
        +---- ImageListPanel -------- 缩略图、选择、移除、右键命令
        +---- ImageView ------------- 显示、变换和 ROI 交互
        +---- ImageExportService ---- 原始/显示格式编码
        +---- AppLogService --------- 分级日志、滚动文件和诊断导出
        +---- AppSettings ----------- 稳定配置键、默认值和范围
        +---- ImageTransferWorkflowController -- 客户上图模式和命令状态
        |        |
        |        +---- PaDeviceController -> PaProtocol -> ILineTransport
        |
        +---- ImageAcquisitionController -- 图像源会话、状态和统计
        |        |
        |        +---- IImageSource -------- LocalReplaySource / 未来 PCIe
        |        +---- FramePresentationController -- 最新帧、限速和统计
        |                 |
        |                 +---- ReplayPresentationScheduler -- 纯时间计算
        |
        +---- ImageFrame -> ImageSession
        |
        +---- IImageAlgorithms -------- BuiltinImageAlgorithms
        |                                  |
        |                                  +-- MtfAnalysis
        |                                  +-- 当前窗宽窗位算法
        |
        +---- pa_core
        |       TiRawImage / PaProtocol / PaDeviceController
        |       AppLogService / AppSettings / 图像算法接口
        |
        +---- pa_transport
                SerialClient（ILineTransport 的 RS422 实现）
```

CMake 目标：

```text
pa_core       图像数据、协议解析、算法接口、导出和显示调度策略，不依赖窗口
pa_transport  串口收发基础设施
pa_host       Qt Widgets 界面，只负责交互和模块组装
pa_host_tests 无界面自动测试，只链接 pa_core
```

## 算法替换

界面只依赖 `IImageAlgorithms`，目前默认使用 `BuiltinImageAlgorithms`。接口包含：

```text
autoWindowLevel  全图自动窗宽窗位
roiWindowLevel   ROI 窗宽窗位
analyzeMtf       ESF / LSF / MTF 分析
exportMtf        分析结果导出
```

拿到旧软件算法源码后，推荐新增 `LegacyImageAlgorithms`，实现上述接口。替换入口在 `MainWindow` 构造时注入，不需要修改 ROI 交互、分析弹窗、图像显示或菜单代码。

如果旧代码只提供部分算法，可以让新实现对已获得的算法调用旧代码，其余功能继续委托给 `BuiltinImageAlgorithms`，避免一次性迁移。

## 主窗口边界

`MainWindow` 不再直接操作 `QListWidgetItem`，也不保存导出格式表或计算 16/17 ms
显示间隔。当前职责分配如下：

```text
ImageListPanel
    保存列表项路径和缩略图状态
    处理选择、批量移除、Delete 和右键菜单
    向主窗口发送路径级命令，不暴露 QListWidgetItem

ImageExportService
    定义 TiRaw、RAW16、PNG、TIFF、BMP、JPEG 格式元数据
    处理后缀、Qt 格式能力检查和实际写盘
    不打开文件对话框，不依赖 MainWindow

ReplayPresentationScheduler
    根据目标 fps 计算下一次显示延迟
    补偿 Qt 5 整数毫秒定时器误差和迟到帧
    不持有图像、不依赖事件循环，可通过纯数值自动测试

FramePresentationController
    接收图像源输出，只保留最新待显示帧
    管理呈现定时器、实际 FPS、显示计数和丢帧计数
    不读取文件、不渲染图像，可直接复用于未来 PCIe 图像源

ImageAcquisitionController
    连接任意 IImageSource 和 FramePresentationController
    管理 Idle / Starting / Running / Stopping / Error 会话状态
    统一源停止、呈现停止、最终统计和错误转发
    通过会话代际忽略旧图像源的迟到回调
    不解析 PCIe、TiRaw 或业务协议

ImageTransferWorkflowController
    管理 Manual / Continuous 客户模式，模式选择不发送命令
    将开始操作映射为 SEND_SINGLE 或 START_CONTINUOUS
    将停止操作映射为单帧和持续模式共用的 STOP_TRANSFER
    停止写出成功后立即恢复 Ready，不等待设备响应
    持续运行时锁定模式和开始入口，保留停止入口
    不解析 ASCII 文本，不读取或显示图像

MainWindow
    打开文件对话框并维护 ImageSession
    连接列表、上图工作流、采集会话、导出服务和 ImageView
    连接日志显示、设备状态、显示缓存和状态标签
```

## 日志与配置

`MainWindow` 不直接创建 `QSettings`，也不负责拼接日志时间。当前边界如下：

```text
AppSettings
    保留现有 paths/lastImageDirectory 等配置键
    保存串口、波特率、命令超时和最近使用目录
    对波特率和超时执行统一范围限制

AppLogService
    生成 timestamp / level / category / message 结构化条目
    同一条日志通过信号发送给界面，并写入 UTF-8 文件
    默认单文件 5 MiB，保留 5 个 pa_host.N.log 归档
    原子导出环境信息、调用方元数据和文本日志
```

运行日志 Dock 默认隐藏，不占用右侧图像操作区域。日志文件位于
`QStandardPaths::AppLocalDataLocation/logs`，因此 Kylin 和 Windows 都写入当前用户的
应用数据目录，不写工程目录。诊断导出明确不接收 `TiRawImage` 或像素缓冲区；当前只
包含应用版本、Qt/OS/CPU、设备状态、串口参数和文本日志。日志中可能包含用户主动打开
的文件路径，但不会嵌入图像内容。

## 图像输入链路

当前已经有统一的输入契约：

```text
本地单文件 / 本地连续回放 / 未来 PCIe
              -> IImageSource（完整 ImageFrame）
              -> ImageAcquisitionController（会话状态、停止和统计）
              -> FramePresentationController（最新帧与呈现节拍）
              -> ImageSession（当前帧与算法调用）
              -> MainWindow / ImageView
```

`LocalReplaySource` 已接入文件菜单，可选择多个 `.tiraw` 并以 1~120 fps 循环回放。它只负责产生完整帧、错误和统计；`ImageAcquisitionController` 负责源与呈现器的连接、启动、停止和汇总统计，窗口不再直接管理源信号。连续同尺寸帧保留当前视图变换。源和采集会话分别使用运行代际隔离快速重启后的旧回调。

当前回放序列固定为 2～3 帧，因此 `LocalReplaySource` 在启动时一次性加载有效帧，后续循环通过 Qt 隐式共享复用像素内存。内容固定的帧通过 `ImageFrame::contentCacheKey` 明确允许缓存；该键为空的动态帧不进入显示图、`QPixmap` 或全图统计缓存，也不会按来源名加入本地图像列表。主窗口最多缓存 4 张窗宽窗位映射后的 `QImage`，`ImageView` 的 `QPixmap` 缓存上限为 128 MiB。`FramePresentationController` 只保留最新待显示帧，显示节拍跟随用户设置的 1～120 fps；其内部使用 `ReplayPresentationScheduler` 的累计纳秒时间基准补偿 Qt 5 整数毫秒定时器误差。来不及显示的中间帧计入显示丢帧，不进入无界队列。右侧全图 ROI 统计在首帧提交后延迟执行，最多每秒调度一次，并按稳定内容键缓存结果。

主窗口的普通图像列表保存文件路径和缩略图，不缓存每一项的完整 16-bit 像素。缩略图直接从原始像素降采样到目标尺寸，不构造完整显示图。用户切换列表项时重新加载对应文件，避免大量图像同时常驻造成内存快速增长。列表移除只删除 UI 项，不操作源文件。只有用户主动启动的小序列回放会在运行期间缓存所选原始帧。

导出分为两类：`TiRaw` 和 `RAW16 LE` 由 `TiRawImage` 直接写出原始 16-bit 像素；PNG、TIFF、BMP、JPEG 由当前窗宽窗位渲染结果写出 8-bit 显示图像。原始导出使用 `QSaveFile` 原子提交，避免失败时留下不完整文件。DCM 在正式 DICOM 元数据和编码要求确认前不实现。

`TiRawImage` 同时支持：

```text
load(path)                         从本地文件加载
loadData(bytes, sourceName, error) 从内存帧加载
```

PCIe 接入后建议的数据流：

```text
PCIe 驱动/厂商 SDK
        -> 采集线程
        -> 有界帧队列
        -> 帧协议校验与组帧
        -> TiRawImage::loadData
        -> 主线程更新当前图像
        -> 窗宽窗位映射
        -> ImageView 显示
```

采集线程不能直接操作 `MainWindow` 或 `ImageView`。它只应输出完整帧和采集状态，主线程通过 Qt queued signal 接收。队列必须有容量上限；实时显示来不及处理时丢弃旧显示帧，不能无限积压。

新 PCIe 实现应继承 `IImageSource` 或在其适配层输出同样的 `ImageFrame`，无需修改 `ImageSession`、图像算法和窗口代码。当前 `experimental/pcie` 是未参与编译的接口草案，不能当作驱动实现使用。

正式 PCIe 协议如果不是 `.tiraw` 带头格式，应新增独立解码器，将硬件帧转换为 `TiRawImage`，不要在 UI 中解析字节。

## 控制链路

RS422 只负责控制命令和状态，当前依赖方向为：

```text
MainWindow
    +-> ImageTransferWorkflowController（正式客户按钮）
    |       -> PaDeviceController
    |
    +-> PaDeviceController（研发原始命令入口）
        -> PaProtocol
        -> ILineTransport
            -> SerialClient -> ARM
```

职责分配：

```text
MainWindow          连接客户按钮、研发菜单和状态显示，不解析 ASCII 文本
ImageTransferWorkflowController  管理手动/持续模式、开始/停止和按钮可用状态
PaDeviceController  管理连接状态、单条在途命令、5 秒超时和 STATUS 数据
PaProtocol          定义命令文本并将响应行拆成 keyword 和 key=value
ILineTransport      定义打开、关闭、发送行和接收行，不依赖 QSerialPort
SerialClient        实现 Linux/Windows 串口参数、收发缓存和完整行切分
```

控制器同一时间只允许一条普通命令在途，防止响应无法对应命令。命令执行期间界面禁用其他 PA/FPGA 命令；响应成功后恢复 `Ready`，设备返回 `ERR`、响应超时或传输错误后进入 `Error`。串口仍打开时允许从错误状态直接重试，断开连接会取消在途命令。`STOP_TRANSFER` 是例外：它会取消当前本地等待和计时，写出成功后立即完成，不建立新的在途命令。

正式客户流程不会直接选择协议命令。Idle 和 Continuous 只修改本地模式，客户点击开始后
才分别发送 `SEND_SINGLE` 或 `START_CONTINUOUS`。持续发送由 FPGA 维持，直到客户点击
停止。`STOP_TRANSFER` 写出后不等待回包，上位机立即取消原命令等待和超时计时，并恢复
客户按钮。V0 没有事务号，停止后迟到的旧响应与后续命令仍可能混淆；该可靠性问题已记录
在协议草案中，基础功能阶段不以等待停止回包的方式阻塞界面。原始 `PA/FPGA` 菜单和运行日志只作为当前研发、
维护入口，发布阶段应由维护模式控制可见性。

自动测试使用内存模拟的 `ILineTransport`，不需要串口硬件即可验证连接、状态解析、迟到响应隔离、重复命令拦截、超时、错误恢复和打开失败。

图像数据走光口/PCIe，不应混入 `SerialClient`。未来如果协议改成二进制或增加校验，只替换 `PaProtocol`、控制器及对应传输边界。

FPGA 通过 MSI/MSI-X 产生的硬件中断只属于 PCIe 驱动内部，用于完成 DMA 或唤醒阻塞读取。RS422 业务协议不暴露 `WAIT_IRQ`、通用 IRQ 计数或其他硬件实现细节；ARM 需要主动通知上位机时，应转换为校准完成、设备故障等带明确语义的事件。

## 后续实施顺序

```text
1. 用命令行 PCIe 采集工具稳定接收并保存单帧
2. 明确帧头、长度、序号、时间戳和校验规则
3. 实现采集线程和有界帧队列
4. 通过内存入口接入 TiRawImage
5. 跑通连续显示、停止、错误恢复和丢帧统计
6. 拿到旧算法源码后实现新的 IImageAlgorithms
7. 最后再做性能优化和正式校准流程
```

这个顺序可以先验证基础链路，同时保证图像算法替换不会影响硬件接入和界面交互。
