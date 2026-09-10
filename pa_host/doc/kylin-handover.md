# PA Host Kylin 交接文档

本文档用于后续在 Kylin-Server-V10-SP3-2403 机器上继续开发。

## 目标环境

```text
操作系统：Kylin-Server-V10-SP3-2403
CPU：海光 7420D，x86_64 兼容
工程目录：/home/zhe/app/pa_host
ARM 端工程：/home/zhe/app/pa_controller
```

## 需要安装的开发包

优先使用系统仓库安装：

```sh
sudo yum install -y gcc gcc-c++ make cmake
sudo yum install -y qt5-qtbase-devel qt5-qtserialport-devel
```

如果系统使用 `dnf`：

```sh
sudo dnf install -y gcc gcc-c++ make cmake
sudo dnf install -y qt5-qtbase-devel qt5-qtserialport-devel
```

如果包名不一致，先用下面命令查：

```sh
yum search qt5 | grep -i serial
yum search qt5 | grep -i base
```

## 环境检查命令

安装后请在 Kylin 机器上执行，并把输出给我看：

```sh
uname -a
cat /etc/os-release
gcc --version
g++ --version
cmake --version
qmake --version
pkg-config --modversion Qt5Core Qt5Widgets Qt5SerialPort
ls -l /dev/ttyS* /dev/ttyUSB* 2>/dev/null
groups
```

如果普通用户没有串口权限，一般需要加入对应用户组。常见组名是 `dialout`、`uucp` 或 `tty`，以 Kylin 实际设备权限为准：

```sh
ls -l /dev/ttyS0 /dev/ttyUSB0
```

当前 Kylin 机器已确认：

```text
内核：4.19.90-89.11.v2401.ky10.x86_64
GCC/G++：7.3.0
CMake：3.16.5
Qt5Core/Qt5Widgets/Qt5SerialPort：5.11.1
qmake：未安装
串口设备组：dialout
当前用户组：zhe wheel
```

`qmake` 缺失不影响当前工程，因为本工程使用 CMake 构建。只要 `cmake -S . -B build`
能找到 `Qt5Widgets` 和 `Qt5SerialPort` 即可。

串口权限需要处理。当前 `/dev/ttyS*` 权限是 `root:dialout`，而用户 `zhe` 还不在
`dialout` 组，直接打开串口会失败。建议执行：

```sh
sudo usermod -aG dialout zhe
```

执行后需要退出当前桌面/终端会话并重新登录，再用 `groups` 确认输出里包含 `dialout`。
如果只是临时验证，也可以先用 `sudo ./build/pa_host` 运行，但正式使用不建议依赖 root 权限。

## 编译运行

```sh
cd /home/zhe/app/pa_host
cmake -S . -B build
cmake --build build -j
./build/pa_host
```

命令行使用 Makefiles/Ninja 时，工程默认配置为 `RelWithDebInfo`，兼顾优化和调试符号。若 Qt Creator Kit 显式选择了 `Debug`，大尺寸图像的加载、窗宽窗位转换和 ROI 统计可能明显变慢；验证回放性能时应切换到 `RelWithDebInfo` 或 `Release`。

Qt Creator 可以直接打开 `CMakeLists.txt`。需要注意：

```text
1. Qt Creator 会默认创建 shadow build 目录，例如 /home/zhe/app/build-pa_host-Desktop-Default
2. 这类目录和 CMakeLists.txt.user 都是本机构建/IDE 产物，不需要提交
3. 命令行构建目录仍然建议使用 /home/zhe/app/pa_host/build
```

## 当前测试方式

### 0. 先跑自动测试

自动测试不依赖真实串口或外部 `.tiraw` 样例，会在临时目录生成测试图像：

```sh
cd /home/zhe/app/pa_host
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build
ctest --output-on-failure
```

预期结果：

```text
100% tests passed, 0 tests failed out of 1
```

当前覆盖协议解析、`.tiraw` 文件校验、像素读取、自动窗宽窗位、ROI 统计和显示映射。

自动测试也覆盖 `AppSettings`、`AppLogService`、`PaDeviceController`、`ImageTransferWorkflowController`、`ImageSession`、`ImageExportService`、`ReplayPresentationScheduler`、`FramePresentationController`、`ImageAcquisitionController` 与本地连续回放源：包括配置持久化与边界、日志滚动和诊断导出、模拟 RS422 连接、结构化 STATUS、迟到响应隔离、命令超时与错误恢复、手动/持续模式、单帧、持续启动和停止命令、导出格式与写盘、60 fps 时间补偿、最新帧覆盖与丢帧统计、采集会话启停、同步首帧、源销毁、迟到帧隔离、回放帧序号、非循环播放结束、快速停止重启以及坏文件跳过统计。

核心代码已经拆成 `pa_core`、`pa_transport` 和 `pa_host` 三个 CMake 目标。图像列表、
导出编码、客户上图工作流、采集会话、回放呈现和帧率计算也已分别迁移到 `ImageListPanel`、`ImageExportService`、
`ImageTransferWorkflowController`、`ImageAcquisitionController`、`FramePresentationController` 和 `ReplayPresentationScheduler`。图像算法通过 `IImageAlgorithms` 接口调用，后续拿到
旧软件算法源码时不需要修改主窗口。架构说明见 `doc/architecture.md`。

### 1. 先测图像查看

打开菜单：

```text
文件 -> 打开 TiRaw 图像
```

可使用旧 Windows 样例：

```text
/home/zhe/app/windows/tidetector/CollectImage/20260707-1035/*.tiraw
```

预期：

```text
图像能显示
文件选择框支持一次选择多张图像
左侧列表显示缩略图，点击不同项目能切换当前图像
“移除选中图像”、Delete 键和右键菜单能移除列表项，但不会删除源文件
右键“导出当前图像”可导出 TiRaw、RAW16 LE、PNG、TIFF、BMP 和 JPEG
状态栏显示宽高
右侧窗宽窗位能改变显示亮度
自动窗宽窗位会同步更新窗位/窗宽输入框
滚轮和 Ctrl+滚轮都能缩放
左键拖动平移图像
Ctrl+左键拖框弹出“分析测试”窗口
Shift+左键拖框会根据 ROI 重算窗位和窗宽
鼠标移动时右侧会显示像素坐标和值
再次普通左键点击图像会清除当前 ROI 框
缩放、旋转、翻转、保存 PNG 能使用
```

导出说明：TiRaw 和 RAW16 LE 保留原始 16-bit 像素；PNG、TIFF、BMP、JPEG 保存的是当前窗宽窗位对应的 8-bit 显示图像。Kylin 需要存在 Qt TIFF 图像插件才能启用 TIFF 菜单。

### 1.1 测本地回放链路

菜单选择：

```text
文件 -> 回放 TiRaw 序列
```

选择 2～3 张 `.tiraw` 后输入目标帧率。启动时会一次性预加载这些帧，日志显示成功帧数和预加载耗时，之后循环不再读取磁盘。预期首帧立即出现、图像连续刷新、状态栏同时显示实际和目标 fps，且相同尺寸帧不会让缩放或平移回到初始状态。显示节拍支持 1～120 fps，并补偿 Qt 5 整数毫秒定时误差。固定回放帧通过稳定内容键复用 8-bit 显示图、受 128 MiB 限制的 `QPixmap` 和全图统计结果；未来动态帧不提供稳定键时自动禁用这些缓存。通过“文件 -> 停止图像回放”停止，日志会记录输入帧数、显示帧数、显示丢帧和加载失败数。该链路是未来 PCIe 图像源的 UI 验证入口，当前不依赖硬件。

Qt 环境还没装好时，可以先用辅助脚本确认样例文件头：

```sh
cd /home/zhe/app/pa_host
python3 tools/check_tiraw.py /home/zhe/app/windows/tidetector/CollectImage/20260707-1035/20260707-104636-873-00000001.tiraw
```

### 2. 再测 RS422/串口

开发板还没有真实 RS422 时，可以让 ARM 端跑 stdio 模式配合伪终端，或者等真实串口接好后直接选择 `/dev/ttySx`。

真实串口接好后的测试步骤：

```text
1. ARM 板运行 pa_controller，选择实际 RS422 tty。
2. 上位机 pa_host 选择对应串口和波特率。
3. 点击“连接”。
4. 点击“心跳”，日志应收到 OK PONG。
5. 点击“读取状态”，日志应收到 OK STATUS ...
```

顶部正式业务按钮测试：

```text
1. 选择 Idle，不应发送任何命令；点击“手动上图”后发送 SEND_SINGLE。
2. 选择 Continuous，不应发送任何命令；点击“开始上图”后发送 START_CONTINUOUS。
3. 持续上图成功后模式按钮和开始按钮禁用，“停止上图”可用。
4. 点击“停止上图”后发送 STOP_TRANSFER，不注入任何回包，界面应立即恢复可操作状态。
5. 停止后仍保持 Continuous 选择，可以立即再次点击开始。
6. 手动上图等待响应时“停止上图”仍可点击；点击后立即取消本地等待和超时，不等待单帧响应。
7. 未连接和命令不可执行时对应按钮必须明显变灰；任何保持蓝色或绿色的按钮都必须能响应点击。
```

当前没有正式 PCIe 图像源，因此这些按钮只能验证 RS422 业务状态和命令，尚不能让主界面
收到真实 FPGA 图像。`PA/FPGA` 菜单和运行日志属于研发维护入口，不是客户正常操作流程。

未连接时 PA/FPGA 菜单和顶部“手动上图”等命令按钮不可用。命令发送后状态栏显示
“RS422: 执行中”，同一时间不允许重复发送；5 秒内没有收到可识别响应会记录超时并
显示“RS422: 错误”。如果串口仍然打开，可以直接重试，成功响应后恢复“已连接”。

通过“视图 -> 运行日志”可打开底部日志 Dock。文本日志默认写入 Qt 用户应用数据目录
下的 `logs/pa_host.log`，单文件 5 MiB 并保留 5 个归档。现场出现问题时使用“工具 ->
导出诊断信息”，导出文件包含系统环境、串口参数和文本日志，不包含图像像素。串口、
波特率、命令超时和最近目录由 `AppSettings` 保存，不应手工写死到工程源码。

## 当前代码边界

已经完成：

```text
Qt Widgets 主窗口与基础样式
RS422 菜单化连接、断开、发送命令
按行接收 ARM 响应
解析 OK/ERR 和 key=value 状态字段
.tiraw 文件读取、显示与 ROI 统计
旧上位机风格自动窗宽窗位
窗宽窗位、缩放、旋转、翻转、保存
像素值显示
Ctrl+ROI 分析测试弹窗
Shift+ROI 按区域重算窗位窗宽
```

还没完成：

```text
ESF/LSF/MTF 与旧软件完全一致的分析公式
正式校准向导流程
真实光口图像接收
PCIe 采集链路接入
正式二进制协议或带校验协议
高帧率多线程采集/显示优化
安装包制作
```

## 重要判断

旧 Windows 上位机可以参考界面工作流和 `.tiraw` 文件格式，但不建议移植旧通信层：

```text
旧通信：WiFi/TCP/多型号 SDK
新通信：RS422 控制，图像走光口/FPGA
```

所以新上位机应该继续保持专用工程，不要把旧 `TiRayLib.dll` 的网络模型搬过来。

## 当前图像格式判断

`.tiraw` 目前按样例反推为：

```text
0x00: "TiRayRaw" 8 字节
0x08: uint16 version
0x0A: uint16 bytes_per_pixel
0x0C: uint16 height
0x0E: uint16 width
0x10: uint16 little-endian 灰度像素
```

自动窗宽窗位当前已改为接近旧上位机行为的直方图分位算法。对测试图
`20260707-104636-873-00000001.tiraw`，当前程序可算到：

```text
窗位 = 3937
窗宽 = 1948
```

旧上位机记录是 `3939 / 1948`，当前误差可接受。

## 后续接 PCIe 的建议边界

后续光口图像通过 PCIe 接入时，不建议让 UI 直接读硬件，建议拆分为：

```text
PCIe/SDK/驱动
  -> 采集线程
  -> 帧缓冲队列
  -> 原始 16-bit 帧对象
  -> UI 显示
```

第一步应该先做命令行采集测试程序，确认能稳定收一帧并保存成当前程序可打开的 `.tiraw`，再接到 GUI。
当前 `TiRawImage::loadData` 已支持直接解析内存字节流，PCIe 组帧完成后不需要先写临时文件。
