# PA Host

这是新项目的 Linux/Kylin 上位机工程，目标是替代旧 Windows 上位机中与本项目无关的 WiFi、TCP、多型号兼容逻辑。

当前定位：

```text
RS422 与 ARM pa_controller 通讯
提供图像采集、校准和设备参数配置
显示 ARM 返回状态
打开和查看本地 .tiraw 图像
提供旧上位机风格的图像交互和 ROI 分析入口
PCIe 光口图像接收链路
按下位机正式协议进行 RS422 二进制联调
```

## 目录

```text
src/MainWindow.*     Qt 主窗口，负责菜单、图像交互、ROI/分析弹窗、窗宽窗位
src/AppLogService.*  分级运行日志、文件滚动和诊断文本导出
src/AppSettings.*    最近目录、串口参数和命令超时配置
src/ImageListPanel.* 左侧图像列表、缩略图、选择、移除和右键菜单
src/ImageExportService.* 原始图像和显示图像导出服务
src/ImageAlgorithms.* 图像算法接口和当前内置实现，旧算法从这里替换
src/ImageSource.*    图像源抽象接口（供 PCIe 图像源和统一显示链路使用）
src/ImageAcquisitionController.* 图像源会话、状态、停止顺序和统计汇总
src/ImageTransferWorkflowController.* 手动/持续上图模式和客户按钮状态
src/ImageSession.*   当前图像帧、显示渲染和算法调用的应用层边界
src/FramePresentationController.* 最新帧选择、显示限速、实际 FPS 和丢帧统计
src/ReplayPresentationScheduler.* 1～120 fps 显示时间调度策略
src/PaDeviceController.* RS422 设备状态、命令生命周期、响应和超时管理
src/ILineTransport.* 控制链路的二进制帧传输接口，支持模拟传输测试
src/SerialClient.*   RS422 串口二进制帧收发
src/PaProtocol.*     业务命令枚举和界面显示名称
src/PaBinaryProtocol.* RS422 正式二进制帧、CRC 和拆包/粘包解析
src/TiRawImage.*     Windows 样例 .tiraw 16-bit 灰度图读取、自动窗宽窗位、ROI 统计
src/ImageView.*      图像显示、缩放、平移、ROI 框选、保存
doc/kylin-handover.md 给 Kylin 机器继续开发时看的交接文档
doc/architecture.md   模块边界、算法替换和 PCIe 图像链路设计
doc/communication-data-link-draft.md RS422 控制通信与 PCIe 图像数据链路协议草案
doc/git-commit-note-20260710.md 图像交互阶段的中文提交说明
doc/git-commit-note-20260714.md 架构解耦与图像回放阶段的中文提交说明
doc/git-commit-note-20260714-image-list-replay-performance.md 图像列表、导出与回放性能阶段的中文提交说明
doc/git-commit-note-20260714-ui-replay-decoupling.md 图像界面与回放呈现解耦阶段的中文提交说明
doc/git-commit-note-20260714-device-control-decoupling.md RS422 设备控制链路解耦阶段的中文提交说明
doc/git-commit-note-20260714-logging-settings.md 运行日志与应用配置基础设施阶段的中文提交说明
doc/git-commit-note-20260714-communication-protocol-correction.md 通信协议纠正与数据链路草案阶段的中文提交说明
doc/git-commit-note-20260715-image-acquisition-controller.md 图像采集会话解耦阶段的中文提交说明
```

## 构建

### Kylin V10 SP1 安装 Qt 与构建

当前工程使用 CMake 构建，优先查找 Qt6，找不到会自动回退到 Qt5。Kylin V10 SP1 新服务器
`ID_LIKE=debian`，建议先安装 Qt5 开发包；Qt5 已满足本工程的 `Widgets` 和 `SerialPort` 需求。

先确认系统包管理器：

```sh
cat /etc/os-release
command -v apt || command -v dnf || command -v yum
```

如果存在 `apt`，按下面安装：

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
  qtbase5-dev qtbase5-dev-tools libqt5serialport5-dev
```

如果 Kylin 仓库提供 Qt6，也可以改装 Qt6 开发包；当前不是必须：

```sh
sudo apt install -y qt6-base-dev qt6-base-dev-tools libqt6serialport6-dev
```

如果这台机器实际使用 `dnf` 或 `yum`，使用 rpm 系包名：

```sh
sudo dnf install -y gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config \
  qt5-qtbase-devel qt5-qtserialport-devel

# 没有 dnf 时再使用 yum
sudo yum install -y gcc gcc-c++ make cmake ninja-build pkgconfig \
  qt5-qtbase-devel qt5-qtserialport-devel
```

安装后检查 Qt 和 CMake 是否能被找到：

```sh
cmake --version
pkg-config --modversion Qt5Core Qt5Widgets Qt5SerialPort
```

`qmake` 不是必须项；只要 CMake 能找到 `Qt5Widgets` 和 `Qt5SerialPort` 就可以构建。命令行构建：

```sh
cd /home/zhe/app/pa_host
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/pa_host
```

如果系统没有安装 Ninja，可以使用默认 Makefiles：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

如果 CMake 仍然提示找不到 Qt，先查 Qt5 的 CMake 配置目录，再显式指定：

```sh
dpkg -L qtbase5-dev | grep '/cmake/Qt5$'
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)/cmake/Qt5
```

需要图形 IDE 时可以安装 Qt Creator，然后直接打开工程根目录的 `CMakeLists.txt`：

```sh
sudo apt install -y qtcreator
```

在 Makefiles/Ninja 这类单配置生成器下，如果没有显式指定，工程默认使用 `RelWithDebInfo`，即启用优化并保留调试符号。图像加载、窗宽窗位转换和 ROI 扫描不应使用空的 `CMAKE_BUILD_TYPE` 或未优化构建做性能判断。需要完整调试构建时显式使用 `-DCMAKE_BUILD_TYPE=Debug`。

Qt Creator 会默认使用自己的 shadow build 目录，例如 `/home/zkyd/build-pa_host-Desktop-Default`，这属于正常构建产物，不需要提交到 git。

### Windows 构建与调试

Windows 已验证可使用 Qt 5.12.12，并可在 Qt Creator 或 CLion 中直接打开 `CMakeLists.txt`。建议每个 IDE
使用独立构建目录，避免共享 `CMakeCache.txt`。

```text
Qt Creator：选择 Qt 5.12.12 对应的 Desktop Kit
CLion：Toolchain、CMake Profile 和 Qt 编译器保持一致
MinGW 版 Qt 必须配套 MinGW，MSVC 版 Qt 必须配套 MSVC
```

命令行示例：

```bat
cmake -S . -B build-win -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\5.12.12\mingw73_64
cmake --build build-win
```

发布时不能只复制 `pa_host.exe`，需要在 Qt 命令行环境执行：

```bat
windeployqt build-win\pa_host.exe
```

程序会记住最近打开和保存图片的目录。Windows 首次运行默认打开系统“图片”目录；串口列表使用 `COMx`，
Linux/Kylin 使用 `/dev/tty*`。

## 自动测试

测试默认由 CMake 的 `BUILD_TESTING` 选项启用，不依赖 Qt Test 或真实硬件。构建和运行：

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build
ctest --output-on-failure
```

Windows 可在 CLion 的 CTest 面板运行 `pa_host_tests`，也可以在构建目录执行：

```bat
ctest --output-on-failure
```

当前测试覆盖：

```text
PA/ARM 命令字符串和 OK/ERR 响应解析
PA 设备连接、单命令在途、STATUS、超时和传输错误恢复
AppSettings 配置默认值、持久化和数值边界
AppLogService 日志格式、滚动归档和诊断导出
.tiraw 正常文件头、尺寸、像素读取
.tiraw 错误魔数、错误位深、长度不匹配和短文件
0.6% ~ 99.4% 自动窗宽窗位
ROI 均值、最小/最大、标准差和行噪声
手动窗宽窗位的 8-bit 显示映射
缩略图尺寸下的直接降采样显示映射
图像算法接口的自动/ROI 窗宽窗位契约
内存字节流解析为 TiRawImage
ImageSession 文件加载、当前帧、ROI 和显示渲染
ImageExportService 格式元数据、后缀、RAW16 和显示图导出
ReplayPresentationScheduler 60 fps 补偿、迟到追帧和帧率边界
FramePresentationController 最新帧覆盖、停止丢帧和统计周期重置
ImageAcquisitionController 启停、首帧、源销毁、自然结束和迟到帧隔离
ImageTransferWorkflowController 模式选择、单帧、持续启动、停止和错误恢复
ESF / LSF / MTF 基础分析与 CSV 导出
```

## 代码结构

工程按构建目标拆分：

```text
pa_core       图像数据、协议解析、算法接口和默认算法
pa_transport  RS422 串口收发
pa_host       Qt 界面和模块组装
pa_host_tests 无界面核心测试
pa_image_benchmark 真实 TiRaw 性能基准工具
```

`MainWindow` 只负责模块组装和跨模块工作流：图像列表内部行为由 `ImageListPanel` 管理，文件编码由 `ImageExportService` 管理，客户上图按钮由 `ImageTransferWorkflowController` 管理，图像源启停和会话统计由 `ImageAcquisitionController` 管理，显示限速由 `FramePresentationController` 管理，纯时间计算由 `ReplayPresentationScheduler` 管理。主窗口不直接依赖具体 MTF 或窗宽窗位实现，而是通过 `IImageAlgorithms` 调用。后续拿到旧软件算法源码后，新增接口实现并在程序启动时注入即可。详细边界见 `doc/architecture.md`。

RS422 控制链路由 `PaDeviceController` 管理连接状态、单条在途命令、响应和超时。
主机固定使用正式 RS422 二进制协议，串口收发不会回退到文本协议。设备返回有效响应、发生超时或传输错误后，
控制器会统一恢复或切换错误状态。

### RS422 正式二进制联调

ARM 端默认入口就是正式二进制协议（无需附加参数）。

下位机先在 Ubuntu/Kylin 上编译并部署，然后在开发板运行：

```sh
./pa_controller -d /dev/ttyS1 -b 115200
```

主机端运行：

```sh
./pa_host
```

Windows 下对应为：

```bat
pa_host.exe
```

打开主机后选择实际 RS422 端口和波特率，点击连接。连接成功后主机会自动发送一次二进制
`STATUS`，因此可以先观察“运行日志”中的 `BIN REQ`、`BIN RX` 和结构化状态更新。
当前已接入并可直接测试的二进制命令如下：

| 命令 | 编号 | 主机触发方式 | 说明 |
| --- | ---: | --- | --- |
| `HELLO` | `0x0001` | 协议基础能力 | ARM 已支持，主机当前保留协议层解析入口 |
| `PING` | `0x0002` | 开发窗口/协议基础 | 空 payload，返回 `DONE` |
| `STATUS` | `0x0003` | 连接后自动读取 | 返回设备和工作状态 TLV |
| `VERSION` | `0x0004` | 协议基础能力 | ARM 已支持，用于版本信息读取 |
| `START_STATIC_CAPTURE` | `0x0200` | 手动上图 | 使用 ARM 当前配置执行一次静态采图 |
| `START_DYNAMIC` | `0x0210` | 开始上图 | 使用配置文件中的 Dynamic step 启动 |
| `STOP_DYNAMIC` | `0x0211` | 停止动态模式/停止上图 | 等待 Dynamic 回到停止状态 |
| `QUERY_DYNAMIC` | `0x0212` | 工具 -> Dynamic 查询 | 显示状态、结束/调试值和最终图像地址 |
| `CAL_OFFSET_*` | `0x0300`~`0x0303` | 校准 -> 制作暗场模板 | 初始化、采集、完成确认和取消 |
| `CAL_GAIN_*` | `0x0304`~`0x0307` | 校准 -> 制作亮场模板 | 多灰度级初始化、逐级采集、生成和取消 |
| `CAL_STATUS` | `0x0308` | 校准窗口自动轮询 | 返回后台任务进度和亮场级别状态 |
| `IMG_UPLOAD_*` | `0x0500`~`0x0502` | 校准 -> 查看当前暗/亮场模板 | 配置模板源、触发上传并查询状态 |

配置组命令由“工具 -> 开发”和“工具 -> Dynamic 配置”窗口使用：

| 命令 | 编号 | 作用 |
| --- | ---: | --- |
| `GET_CONFIG_GROUP` | `0x0103` | 读取一组当前参数 |
| `SET_CONFIG_GROUP` | `0x0104` | 校验、应用并保存一组参数 |

配置组 payload 为 `u16 group_id` 加连续的 `{u16 item_id, u16 length=4, u32 value}` 小端字段。
当前组号为 `1=Static`、`2=CORR`、`3=GIC`、`4=ROIC`、`5=Dynamic`。

主机二进制模式已放行基础查询、静态单帧、Dynamic 启停/查询、配置组读写、Offset/Gain
模板制作和模板上传。模板上传复用常驻 PCIe 图像监听，收到的下一帧会标记为当前暗场或亮场模板并显示。

协议帧使用小端字段：`magic=0x55AA`、`version=1`、`header_len=16`、CRC16-CCITT-FALSE，
payload 最大 2048 字节。空 payload 的 `PING` 请求固定测试帧为：

```text
AA 55 01 10 01 00 02 00 01 00 00 00 00 00 00 00 B7 D3
```

实际联调时，主机日志中的 `BIN RX` 会显示帧类型、命令号、序号和 payload；如果看不到
`BIN RX`，先检查 RS422 端口、交叉收发线、波特率和 ARM 是否确实以 `--binary` 启动。

运行日志由 `AppLogService` 统一生成，格式包含时间、级别和来源，并写入应用数据目录下的 `logs/pa_host.log`。单个文件默认最多 5 MiB，保留 5 个归档。通过“视图 -> 运行日志”打开底部日志 Dock；“工具 -> 导出诊断信息”生成包含运行环境、控制参数和文本日志的诊断文件，不包含图像像素。“工具 -> 命令超时设置”可调整并保存响应超时。

构建时启用 `BUILD_TESTING` 后，可以用真实的 2～3 帧序列复测预加载、显示转换、缩略图和全图统计耗时：

```sh
./build/pa_image_benchmark frame1.tiraw frame2.tiraw frame3.tiraw
```

## 当前界面能力

当前版本已经把旧 Windows 上位机里最常用的一段图像查看工作流补齐：

```text
顶部菜单：文件 / RS422 / 校准 / 视图 / 工具 / 帮助
顶部模式条：Idle / Continuous / 手动上图 / 停止上图
左侧：带缩略图的图像列表，可点击切换和移除
中间：图像画布
右侧：图像操作 / 窗宽窗位 / 图像信息
底部：型号 / 序列号 / 连接状态 / 工作模式 / 图像尺寸 / 缩放 / 显示 FPS
```

“帮助 -> 关于 PA Host”显示软件版本、编译时间、ARM 程序版本和 FPGA 版本。设备版本来自
`STATUS` 响应中的可选字段 `arm_version`、`fpga_version`；设备尚未连接或当前 ARM 程序
未返回对应字段时显示“未获取”。

没有图像时，保存、最大化、右侧图像操作和窗宽窗位控件均禁用；自动窗宽窗位开启时，
手动窗位/窗宽控件禁用。型号和序列号可由 `STATUS` 的可选 `model`、`serial` 字段更新。

顶部模式条是正式用户上图入口：

```text
选择 Idle         只选择手动模式，不发送设备命令
点击“手动上图”   发送 SEND_SINGLE，要求 FPGA 发送一张图像
选择 Continuous   只选择持续模式，不发送设备命令
点击“开始上图”   发送 START_CONTINUOUS，FPGA 随后自动持续发送
点击“停止上图”   发送 STOP_TRANSFER，停止当前单帧或持续发送
```

模式选择和开始操作分开，保证客户明确确认后才开始传图。持续上图运行期间模式和开始按钮
锁定，只保留停止按钮；开始命令等待响应时也可以点击停止。`STOP_TRANSFER` 写出后不等待
设备回包，上位机立即取消原命令的本地等待和超时计时，恢复模式选择及开始按钮。停止后
保留 Continuous 选择，允许再次开始。旧的底层调试命令
菜单和“运行日志”当前仅用于研发及维护联调，正式用户流程不依赖这些入口。

所有不可执行的顶部按钮必须显示为灰色；保持蓝色或绿色的按钮必须能够立即响应点击。
未连接时四个顶部按钮全部变灰，等待开始响应时只有“停止上图”保持绿色。停止命令写出
成功后界面立即回到就绪状态，不存在等待停止响应期间的按钮锁定。

图像交互：

```text
滚轮、Ctrl+滚轮：缩放
左键拖动：平移图像
Ctrl+左键拖框：ROI 分析，弹出“分析测试”窗口
Shift+左键拖框：按 ROI 重新计算窗位/窗宽
再次普通左键点击：清除当前 ROI 框
鼠标移动：右侧显示当前像素坐标和值
```

“保存显示图像”会保存当前窗宽窗位、旋转和翻转后的图像方向；缩放倍率和平移仅属于查看
状态，不写入输出文件。连续接收中保留 ROI 框时，下方统计随当前帧继续按该 ROI 更新；
清除框或切换图像后恢复全图统计。

图像输入：

```text
文件 -> 打开图像：可一次选择一张或多张本地 `.tiraw` 图像
```

软件启动后会自动开始 PCIe 图像监听，不需要手动点击开始按钮。监听逻辑是：从 `/dev/idma0_event_0` 等待图像中断，中断到达后先按配置等待，再从 BAR0 读取实际行列、image_id 和最终图像 DDR 地址；根据 BAR0 DMA 地址表将最终 DDR 地址转换为 `/dev/idma0_c2h_0` 的读取偏移，然后按 BAR0[0x00c] 行数、BAR0[0x010] 列数读取 RAW16 图像。内存图像快速解析后立即进入显示队列，保存线程随后在后台保存为 `.tiraw` 文件，文件写完后再加入左侧图像列表。PCIe 图像默认保存到 Qt 应用数据目录下的 `pcie_images` 子目录。BAR0 会自动查找 vendor `0x1b4d`、device `0x6667` 的 `resource0`；如果 BAR0 行列暂时为 0，则使用 3072x7680 作为备用尺寸。PCIe 日志中的阶段耗时含义为：`wait` 等待图像中断，`bar0` 读取帧元数据，`c2h` 读取本帧 RAW16 数据，`parse` 转成显示用内存图像，`emit_total` 从中断到进入显示队列，`queue` 保存线程排队时间，`save` 写 `.tiraw` 文件，`total` 从中断到文件保存完成；`PCIe 图像显示完成` 日志中的 `set_frame`、`controls`、`refresh`、`ui_total` 分别表示 UI 线程接收帧、控件同步、图像渲染刷新和 UI 总耗时。PCIe 监听是常驻图像入口，删除图片、切换历史图像、打开本地图像和发送 `STOP_DYNC` 都不会停止上图中断处理。

PCIe 接收使用 `IImageSource -> ImageAcquisitionController -> FramePresentationController -> ImageSession -> ImageView` 更新路径。连续同尺寸帧不会重置缩放、平移、旋转或翻转状态；状态栏显示实际显示 FPS。

普通图像列表保存文件路径、缩略图和最近使用的少量图像缓存。点击列表项时优先复用最近 12 张
16-bit 原始帧；未命中时再快速读取预览数据。选中一项或多项后，可点击“移除选中图像”、按
Delete，或使用右键菜单移除。移除只影响列表，不会删除磁盘上的源文件。PCIe 监听运行时，点击
历史图像只切换当前显示，不会停止 `/dev/idma0_event_0` 的中断监听。

稳定的本地帧携带 `contentCacheKey`，主窗口最多缓存 12 张当前窗宽窗位下的 8-bit 显示图，`ImageView` 还会在 512 MiB 上限内缓存对应 `QPixmap`，避免无显卡或远程桌面环境反复做显示格式转换。实时 PCIe 动态帧默认不提供该键，因此不会按来源名误用旧缓存。首帧立即提交，处理速度跟不上输入时只保留最新帧并统计显示丢帧，不累积延迟。状态栏只显示实际显示 FPS。全图 ROI 统计延迟到首帧提交后执行，并按稳定内容键缓存。调整窗宽窗位会自动清空相关缓存。

图像列表缩略图直接从 16-bit 原始像素降采样到目标尺寸，不再先生成一张完整的 8-bit 大图，因此打开多张大图时的界面阻塞更小。

右键点击图像列表项可从“导出当前图像”子菜单选择格式：

```text
TiRaw       保留 TiRayRaw 文件头和原始 16-bit little-endian 像素
RAW16 LE    仅导出原始 16-bit little-endian 像素，不包含宽高和文件头
PNG / TIFF  导出当前窗宽窗位映射后的 8-bit 显示图像
BMP / JPEG  导出当前窗宽窗位映射后的 8-bit 显示图像
```

DCM 暂未实现。DICOM 需要明确设备、检查和图像元数据以及编码规范，不能只修改文件扩展名。

配置会保存最近图像、保存、导出和诊断目录，以及最后成功连接的串口、波特率和命令超时。Kylin 使用 Qt 的用户配置目录，Windows 使用当前用户配置，工程目录不会生成配置文件。

Kylin 新服务器使用 WCH RS422 串口，默认端口为 `/dev/ttyWCH0`，允许选择 `/dev/ttyWCH0`、`/dev/ttyWCH1`、`/dev/ttyWCH2`、`/dev/ttyWCH3`。如果需要在系统层面先配置串口，可执行：

```sh
sudo stty -F /dev/ttyWCH0 115200 raw -echo -echoe -echok -echoctl -echoke -crtscts -ixon -ixoff
```

## 图像格式

从旧 Windows 上位机保存的 `.tiraw` 样例反推：

```text
0x00: "TiRayRaw" 8 字节
0x08: uint16 version，样例为 1
0x0A: uint16 bytes_per_pixel，样例为 2
0x0C: uint16 height
0x0E: uint16 width
0x10: uint16 little-endian 灰度像素
```

当前读取逻辑：

```text
1. 检查 16 字节头和 "TiRayRaw" 魔数
2. 读取 little-endian 的 version / bytes_per_pixel / height / width
3. 校验文件大小必须等于 16 + width * height * 2
4. 读取 width * height 个 uint16 灰度像素
5. 计算全图 min/max、自动窗宽窗位、ROI 统计
```

自动窗宽窗位当前不是简单 `min/max` 拉伸，而是按旧上位机样例行为改成接近
`0.6% ~ 99.4%` 直方图分位的算法。对测试图
`20260707-104636-873-00000001.tiraw`，当前程序能算到：

```text
窗位 = 3937
窗宽 = 1948
```

与旧上位机记录的 `3939 / 1948` 基本一致。

这仍然只是当前样例结论，后续拿到正式 SDK 文档或光口图像协议后要再确认。

可用开发辅助脚本检查样例：

```sh
python3 tools/check_tiraw.py /home/zhe/app/windows/tidetector/CollectImage/20260707-1035/20260707-104636-873-00000001.tiraw
```

## 当前边界

已经完成：

```text
Qt Widgets 主窗口与样式整理
RS422 菜单化控制入口
.tiraw 16-bit 灰度图读取
旧上位机风格自动窗宽窗位
缩放、平移、旋转、翻转、保存
像素值显示
ROI 统计
Ctrl+ROI 分析测试弹窗
Shift+ROI 按区域重算窗位窗宽
图像算法接口与默认实现解耦
多图打开、缩略图列表、点击切换和批量移除
图像列表右键导出 TiRaw、RAW16、PNG、TIFF、BMP 和 JPEG
连续帧实际 FPS、错误和完成统计
RS422 命令状态、超时和结构化 STATUS 处理
图像采集会话状态、统一停止顺序和迟到帧隔离
主界面手动/持续上图业务状态和三条独立控制命令
分级滚动日志、运行日志 Dock 和诊断文本导出
串口参数、命令超时和最近目录持久化
相同尺寸连续帧保持当前图像视图状态
ESF / LSF / MTF 曲线 CSV 导出
核心模块无硬件自动测试
```

还没完成：

```text
真实光口/PCIe 图像接收
ESF/LSF/MTF 与旧软件完全一致的公式校准
正式校准流程
高帧率实时显示与多线程采集管线
安装包与部署脚本
```
