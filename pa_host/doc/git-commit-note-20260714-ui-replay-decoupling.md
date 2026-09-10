# 2026-07-14 图像界面与回放呈现解耦提交说明

## 提交主题

```text
解耦图像界面与回放呈现职责
```

## 阶段目标

本阶段不调整现有图像操作和回放交互，重点降低 `MainWindow` 对具体控件、导出格式和
显示调度细节的依赖，为后续替换旧软件算法、接入 PCIe 图像源和扩展自动测试建立稳定
边界。

改动遵循以下原则：

```text
界面列表内部状态不暴露给主窗口
文件编码与文件对话框分离
回放时间算法与 Qt 事件循环状态分离
输入帧选择、显示限速和统计不由主窗口维护
实时显示只保留最新待显示帧，不建立无界队列
```

## 主要改动

### 1. 拆分 ImageListPanel

新增 `ImageListPanel`，集中管理：

```text
图像路径和缩略图状态
当前图像选择与滚动定位
批量移除、Delete 快捷键和右键菜单
导出格式菜单及格式可用状态
```

模块只向外发送文件路径、移除数量和导出格式标识，不向 `MainWindow` 暴露
`QListWidgetItem`。列表移除仍只影响界面，不删除源文件。

### 2. 拆分 ImageExportService

新增无界面的 `ImageExportService`，统一管理 TiRaw、RAW16 LE、PNG、TIFF、BMP 和
JPEG 的格式信息、默认后缀、Qt 编码能力检查及实际写盘。

职责边界如下：

```text
MainWindow          负责保存文件对话框和用户提示
ImageExportService  负责格式识别、补全后缀和编码写盘
TiRawImage          负责原始 16-bit TiRaw/RAW 数据输出
```

### 3. 拆分 ReplayPresentationScheduler

新增纯时间算法 `ReplayPresentationScheduler`，根据目标 `1～120 fps` 计算下一次
显示延迟，并使用累计纳秒基准处理 Qt 5 整数毫秒定时器的 16/17 ms 补偿和迟到追帧。

该模块不持有图像、不依赖 `QTimer` 或窗口，可通过固定数值直接测试。

### 4. 新增 FramePresentationController

新增 `FramePresentationController`，作为 `IImageSource` 与 `MainWindow` 之间的呈现层：

```text
接收任意 ImageFrame
只保留最新待显示帧
旧待显示帧被覆盖时计入 droppedFrames
按目标帧率向界面发出 framePresented
统计 submittedFrames、presentedFrames 和 droppedFrames
每 500 ms 更新一次实际显示 FPS
停止时将尚未显示的帧计入丢帧
重新开始时建立独立统计周期
```

控制器内部组合 `ReplayPresentationScheduler`，前者负责 Qt 事件循环和帧状态，后者只
负责时间计算。未来 PCIe 图像源只需继续输出完整 `ImageFrame`，不需要复制这套限速
和丢帧逻辑。

### 5. 收敛 MainWindow 职责

`MainWindow` 不再：

```text
直接创建或查找 QListWidgetItem
保存导出格式表和执行具体编码
计算下一次回放显示时间
保存待显示帧、FPS 时钟和显示/丢帧计数
```

主窗口现在负责模块组装、文件对话框、当前 `ImageSession`、界面标签和显示缓存。
`imageRefreshTimer_` 仅用于窗宽窗位等静态界面操作；回放控制器选出的帧直接进入显示
刷新，不再经过第二层回放排队。

本阶段将 `MainWindow.cpp` 收敛到约 1189 行，拆出的职责分别落入可独立维护和测试的
模块。

### 6. 补充必要中文注释

在模块职责、最新帧覆盖、计时起点、停止顺序和呈现后再次调度等非直观逻辑处增加
中文注释。没有为普通赋值、简单条件判断等自解释代码增加流水账注释。

### 7. 增加自动测试

新增测试覆盖：

```text
ImageExportService 格式查询、后缀补全和原始/显示图导出
ReplayPresentationScheduler 60 fps 补偿、迟到追帧和帧率边界
FramePresentationController 连续输入只呈现最新帧
待显示帧覆盖和停止时的丢帧统计
重新启动后的统计周期重置
```

## 当前图像链路

```text
LocalReplaySource / 未来 PCIe ImageSource
    -> FramePresentationController
    -> MainWindow::handlePresentedFrame
    -> ImageSession
    -> ImageView
```

算法链路仍通过 `IImageAlgorithms` 隔离。后续获得旧软件算法源码时，可以新增接口实现
并注入 `ImageSession`，不需要修改列表、回放呈现或图像交互代码。

## 文档更新

```text
README.md             增加新模块、测试项和完整回放链路说明
doc/architecture.md   更新主窗口边界、呈现控制器和 PCIe 数据流
doc/kylin-handover.md 更新 Kylin 自动测试和模块交接说明
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
本机使用 Qt 5 构建
pa_core、pa_transport、pa_host、pa_host_tests、pa_image_benchmark 编译成功
CTest：1/1 测试通过
git diff --check：通过
Xvfb 无界面启动烟测正常，2 秒后由 timeout 终止
```

## 当前边界

```text
本地固定序列仍可使用按路径的 QImage/QPixmap 和全图统计缓存
未来 PCIe 动态帧不能按来源路径复用固定图像缓存
采集线程只能通过 queued signal 输出完整帧，不能直接操作窗口
高输入速率下显示层丢弃旧帧并保持最新状态，不积压延迟
正式 FPGA 帧协议、DMA 缓冲区所有权和错误恢复策略仍待硬件联调确认
```
