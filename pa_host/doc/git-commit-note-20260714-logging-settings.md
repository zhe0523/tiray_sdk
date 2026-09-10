# 2026-07-14 运行日志与应用配置基础设施提交说明

## 提交主题

```text
完善运行日志与应用配置基础设施
```

## 阶段目标

本阶段补齐应用长期运行和现场排障所需的基础设施，把散落在 `MainWindow` 中的界面日志
与 `QSettings` 访问收敛为独立模块。改造后，设备控制、图像操作、分析和系统事件使用同一
套分级日志；串口参数、命令超时和最近使用目录通过稳定配置接口读写。

本次不修改图像算法、RS422 协议和 PCIe 图像链路，也不在诊断文件中保存图像像素。

## 主要改动

### 1. 新增 AppLogService

新增 `AppLogService`，统一生成包含时间、级别、来源和消息的结构化文本日志。支持以下
四个级别：

```text
Debug
Info
Warning
Error
```

每条日志同时通过 Qt 信号发送给界面，并持久化到用户应用数据目录。Kylin 当前默认路径
为：

```text
/home/zhe/.local/share/TiRay/PA Host/logs/pa_host.log
```

日志不会写入源码目录或构建目录。

### 2. 增加日志滚动

当前滚动策略为：

```text
单个 pa_host.log 最大 5 MiB
最多保留 5 个归档文件
归档名为 pa_host.1.log 至 pa_host.5.log
```

服务在写入前检查文件大小，达到上限时依次移动归档并重新打开当前日志。目录创建、文件
打开、写入和滚动失败会通过 `persistenceError` 报告，不让日志故障中断主业务流程。

### 3. 增加诊断文本导出

“工具 -> 导出诊断信息”可生成现场排障文件，内容包括：

```text
应用名称和版本
Qt 版本
操作系统和 CPU 架构
调用方提供的串口、波特率和命令超时等元数据
当前及归档文本日志
```

诊断导出不接收 `TiRawImage` 或像素缓冲区，因此不会包含图像像素。文本日志会记录用户
执行的操作，可能包含打开、保存或导出文件的本地路径，向外发送诊断文件前仍应检查。
为避免破坏运行日志，导出路径不能覆盖当前 `pa_host.log`。

### 4. 新增 AppSettings

新增 `AppSettings` 作为应用配置边界，集中维护键名、默认值和范围。保留已有最近目录键：

```text
paths/lastImageDirectory
paths/lastSaveDirectory
paths/lastExportDirectory
```

新增配置键：

```text
paths/lastDiagnosticDirectory
control/serialPort
control/serialBaudRate
control/commandTimeoutMs
```

波特率限制为 `1200～3000000`，命令响应超时限制为 `100～300000 ms`，默认超时为
`5000 ms`。越界或无效的持久化数值会回退到安全范围，避免损坏的用户配置直接进入
设备控制层。

默认构造继续使用 Qt 当前用户配置；测试可注入独立 INI 文件，不污染实际应用配置。

### 5. 接入 MainWindow

`MainWindow` 不再直接创建 `QSettings` 或拼接界面日志。当前接入范围包括：

```text
RS422 连接、发送、接收、命令完成和错误
图像打开、移除、导出和回放状态
ROI、分析测试和窗宽窗位操作
日志启动、诊断导出和应用退出
```

退出时记录正常退出事件并同步配置。串口、波特率和命令超时会恢复到上次保存值，最近
图像、保存、导出和诊断目录也通过同一配置对象维护。

### 6. 增加运行日志界面

新增默认隐藏的底部日志 Dock，通过“视图 -> 运行日志”显示。Dock 最多保留 2000 行，
避免长时间运行导致界面文本无限增长；默认隐藏也不会占用右侧图像操作区域。

“工具 -> 命令超时设置”允许在受控范围内调整响应超时，并立即同步到
`PaDeviceController` 和持久化配置。

### 7. 注入应用版本

CMake 将 `${PROJECT_VERSION}` 以 `PA_HOST_VERSION` 编译定义注入可执行程序，
`main.cpp` 再设置 Qt 应用版本。诊断文件由此可以记录与当前二进制一致的版本信息。

### 8. 增加自动测试

新增测试覆盖：

```text
AppSettings 默认值和目录持久化
串口、波特率和命令超时持久化
波特率和超时数值边界
AppLogService 日志格式和界面信号
日志文件写入和多级滚动归档
诊断文本内容和元数据
诊断覆盖当前日志的保护
```

已有设备控制、图像会话、导出和回放测试继续执行。

## 文档更新

```text
README.md             增加日志、配置、界面入口和测试说明
doc/architecture.md   增加日志与配置模块边界及数据范围
doc/kylin-handover.md 增加 Kylin 日志位置、诊断导出和配置检查项
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
本机 Qt 5 全量配置和构建成功
CTest：1/1 测试通过
git diff --check：通过
Xvfb 无界面启动烟测正常，2 秒后由 timeout 终止
真实运行日志成功写入用户应用数据目录
工程目录未生成 .ini 或 pa_host*.log
```

## 当前边界

```text
日志是本地文本文件，当前未实现压缩、上传或远程收集
诊断文件不包含图像像素，但文本日志可能包含用户访问的文件路径
日志 Dock 只用于运行观察，不替代持久化日志
命令超时仍需 ARM 实机联调后确认合理默认值
正式 PCIe 帧协议和硬件错误信息等待 FPGA/驱动接口确定
```
