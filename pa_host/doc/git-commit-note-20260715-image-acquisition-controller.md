# 2026-07-15 图像采集会话解耦提交说明

## 提交主题

```text
解耦图像采集会话与动态帧缓存
```

## 阶段目标

本阶段继续完善未来 PCIe 图像源所需的应用框架，把图像源启停、呈现限速、错误转发、
停止顺序和统计汇总从 `MainWindow` 中迁移到独立采集会话控制器。

当前仍不接入真实 PCIe 驱动，不固化 FPGA 帧协议，也不修改旧软件算法。现有本地 TiRaw
回放继续作为无硬件图像源，用于验证与未来实时源相同的应用层路径。

## 主要改动

### 1. 新增 ImageAcquisitionController

新增 `ImageAcquisitionController`，位于任意 `IImageSource` 和
`FramePresentationController` 之间：

```text
IImageSource
    -> ImageAcquisitionController
        -> FramePresentationController
            -> MainWindow / ImageSession
```

控制器提供以下会话状态：

```text
Idle      没有活动图像源
Starting  正在启动图像源
Running   图像源和呈现器均已运行
Stopping  正在执行统一停止顺序
Error     启动失败或图像源意外销毁
```

### 2. 统一图像源生命周期

控制器负责：

```text
连接和断开图像源信号
启动图像源并记录启动耗时
启动呈现限速和 FPS 统计
转发源错误、完整帧和 FPS
显式停止图像源和呈现器
汇总输入、显示、丢帧和源失败统计
自然结束与显式停止使用不同末帧策略
```

显式停止时，尚未呈现的最新帧计入显示丢帧；有限图像源自然结束时，先呈现最后一张待
显示帧，再结束会话，避免最后一帧无故消失。

### 3. 增加会话代际隔离

每次启动和停止都会推进内部会话代际。源信号连接捕获启动时的代际，旧会话的排队回调
即使在新会话开始后才到达，也不能进入新的呈现链路。

控制器还处理：

```text
start() 返回前同步发出的首帧
空图像源
图像源启动失败
图像源运行期间被销毁
重复 stop() 不重复产生结束统计
快速停止后切换到另一个图像源
```

### 4. 收敛 MainWindow

`MainWindow` 不再直接连接：

```text
LocalReplaySource::frameReady
LocalReplaySource::sourceError
LocalReplaySource::runningChanged
FramePresentationController::framePresented
FramePresentationController::fpsUpdated
```

窗口只负责配置本地回放文件和目标帧率，然后调用采集控制器的 `start/stop`，消费统一的
帧、FPS、错误和最终统计信号。

### 5. 增加稳定内容缓存键

`ImageFrame` 新增可选的 `contentCacheKey`：

```text
非空  当前帧内容在会话内稳定，可以复用显示图、QPixmap 和全图统计
为空  当前帧视为动态内容，每次重新渲染和统计
```

本地文件和预加载回放使用绝对文件路径作为稳定键。未来 PCIe 动态帧默认不设置该键，
因此即使连续帧使用相同来源名，也不会错误复用上一帧像素结果。

动态帧也不会自动加入左侧本地图像列表。图像列表继续只管理具有稳定文件身份的内容。

### 6. 泛化窗口缓存命名

窗口中的回放专用命名调整为帧内容语义：

```text
replayDisplayCache_          -> frameDisplayCache_
replayFullImageStatsCache_   -> stableFrameStatsCache_
clearReplayDisplayCaches()   -> clearFrameDisplayCaches()
```

缓存策略不再通过 `replaySource_->isRunning()` 判断，而是只读取当前
`ImageFrame::contentCacheKey`。

### 7. 扩展自动测试

新增 `FakeImageSource` 和采集控制器测试，覆盖：

```text
Idle / Starting / Running / Stopping / Error 状态
空源和启动失败
启动前同步首帧
连续输入只呈现最新帧
显式停止和最终统计
自然结束保留最后一帧
源错误转发
图像源意外销毁
旧源迟到帧不能进入新会话
重复停止不重复产生结束事件
本地文件和回放帧稳定缓存键
```

## 文档更新

```text
README.md             增加采集会话控制器、测试和缓存策略
doc/architecture.md   更新图像输入依赖方向和主窗口边界
doc/kylin-handover.md 增加采集会话测试及动态帧缓存说明
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
主窗口不存在 replaySource_->isRunning() 缓存判断残留
git diff --check：通过
Xvfb 无界面启动正常，2 秒后由 timeout 终止
```

## 当前边界

```text
当前正式图像源仍只有本地 TiRaw 回放
PCIe 帧头、驱动接口和缓冲区所有权仍未冻结
contentCacheKey 是主机内部缓存身份，不是协议 acquisition_id
采集控制器只管理图像数据会话，不发送 RS422 业务命令
单帧、连续和停止上图的业务协调仍需独立工作流控制器
```
