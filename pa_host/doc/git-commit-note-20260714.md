# 2026-07-14 阶段提交说明

## 提交主题

```text
解耦图像处理链路并增加本地序列回放
```

## 阶段目标

本阶段不接入尚未准备好的 FPGA/PCIe 硬件，优先完成上位机内部结构调整和无硬件验证链路：

```text
本地单文件 / 本地连续回放 / 未来 PCIe
        -> 统一图像帧入口
        -> 当前图像会话
        -> 窗宽窗位与分析算法
        -> 图像显示、列表、信息和状态栏
```

这样后续获得旧软件算法源码或正式 PCIe 协议时，可以替换对应实现，不需要重新修改图像交互和主窗口流程。

## 主要改动

### 1. 拆分 CMake 构建目标

工程由单个可执行程序拆分为：

```text
pa_core       图像数据、协议解析、算法接口、图像源和图像会话
pa_transport  RS422 串口收发
pa_host       Qt Widgets 主界面和模块组装
pa_host_tests 无界面核心自动测试
```

所有目标统一启用 GCC/Clang 的 `-Wall -Wextra -Wpedantic`，MSVC 使用 `/W4 /permissive-`。

### 2. 增加图像算法边界

新增 `IImageAlgorithms` 与默认实现 `BuiltinImageAlgorithms`，接口包含：

```text
autoWindowLevel  全图自动窗宽窗位
roiWindowLevel   ROI 窗宽窗位
analyzeMtf       ESF / LSF / MTF 分析
exportMtf        CSV 曲线导出
```

`MainWindow` 不再直接实现或调用具体算法。后续可新增 `LegacyImageAlgorithms` 接入旧软件原始算法，保留现有 UI、ROI 交互和显示路径。

### 3. 独立 MTF 分析模块

将原先主窗口内的临时曲线计算迁移到 `MtfAnalysis`：

```text
识别横向或纵向斜边
四倍过采样生成 ESF
Savitzky-Golay 导数生成 LSF
离散频域计算并归一化 MTF
导出 esf.csv、lsf.csv、mtf.csv
```

当前算法用于跑通分析链路，尚未与旧上位机全部样例完成严格数值校准。拿到原始算法后应通过 `IImageAlgorithms` 替换。

### 4. 增加内存图像入口

`TiRawImage` 新增：

```cpp
loadData(const QByteArray& data, const QString& sourceName, QString* errorMessage)
```

本地文件读取和未来 PCIe 内存帧共用同一套 `.tiraw` 头、尺寸、位深和数据长度校验。PCIe 组帧完成后不需要先写临时文件再加载。

### 5. 增加统一图像源与会话

新增：

```text
ImageFrame         完整图像、来源名、序号和接收时间
IImageSource       启动、停止、运行状态、帧、错误和统计接口
LocalReplaySource  本地 .tiraw 序列回放实现
ImageSession       当前帧、渲染、ROI、窗宽窗位和 MTF 调用入口
```

主窗口由直接持有 `TiRawImage` 改为持有 `ImageSession`。本地文件、回放帧和未来硬件帧进入相同的应用层路径。

### 6. 接入本地 TiRaw 连续回放

文件菜单新增：

```text
回放 TiRaw 序列
停止图像回放
```

用户可选择多张 `.tiraw`，设置 `1~120 fps` 后循环播放。运行期间：

```text
状态栏显示实际 FPS
日志记录开始、停止、成功帧数和失败帧数
坏文件产生错误日志并跳过
同尺寸帧保留缩放、平移、旋转和翻转状态
尺寸变化时重新适应图像
右侧全图 ROI 统计最多每秒刷新一次
```

该功能用于在 PCIe 硬件到位前验证连续帧刷新、状态更新和错误处理路径。

### 7. 增加核心自动测试

测试程序不依赖串口、PCIe、外部样例或 Qt Test，在临时目录自行生成 `.tiraw`。当前覆盖：

```text
PA/ARM 命令与响应解析
.tiraw 正常解析和错误文件校验
内存字节流图像解析
像素读取、ROI 统计和 8-bit 显示映射
自动窗宽窗位和 ROI 窗宽窗位
图像算法接口契约
ImageSession 文件加载和渲染
LocalReplaySource 连续帧、序号、停止和错误统计
ESF / LSF / MTF 基础结果和 CSV 导出
```

### 8. 保留未编译 PCIe 原型

`experimental/pcie` 增加主机侧接口草案，包括帧同步、RAW12 解包和 C2H 读取思路。

该目录明确：

```text
不加入 CMakeLists.txt
不连接 MainWindow 或 ImageView
不依赖 /home/zhe/pcie/pcie 的旧验证实现
不固化尚未确认的 FPGA 帧头、尺寸和打包方式
不能作为正式驱动或采集程序使用
```

待 FPGA 协议确认后，应让正式实现输出 `ImageFrame` 并接入 `IImageSource`。

## 文档更新

```text
README.md             构建、自动测试、模块结构、回放操作和当前边界
doc/architecture.md   算法替换、图像输入、控制链路和 PCIe 接入边界
doc/kylin-handover.md Kylin 环境、自动测试、本地回放和后续硬件验证步骤
```

## 验证记录

提交前执行：

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build
ctest --output-on-failure
```

验证结果：

```text
pa_core、pa_transport、pa_host、pa_host_tests 编译成功
CTest：1/1 测试通过
git diff --check：通过
QT_QPA_PLATFORM=offscreen 启动烟测：程序未崩溃，2 秒后由 timeout 终止
```

## 当前限制

```text
PCIe 真实采集尚未接入
FPGA 帧协议、RAW12 打包、校验和设备节点尚未确认
回放源当前在 Qt 主线程读取和解析文件，只用于功能链路验证
大尺寸高帧率显示仍需要采集线程、有界队列和丢旧帧策略
自动窗宽窗位是当前样例反推算法
ESF / LSF / MTF 尚未与旧软件原始实现完全对齐
正式校准流程和安装包尚未完成
```

## 下一阶段建议

```text
1. 等 FPGA 准备后确认帧头、尺寸、字节序、RAW12 打包和校验规则
2. 先实现命令行单帧采集与保存，验证 PCIe 基础链路
3. 实现后台采集线程、有界帧队列、丢帧和错误统计
4. 让正式 PCIe 图像源输出 ImageFrame，复用当前 ImageSession 和 UI
5. 获取旧软件算法源码后实现 LegacyImageAlgorithms
6. 使用固定样例和 ROI 建立新旧算法回归数据
```
