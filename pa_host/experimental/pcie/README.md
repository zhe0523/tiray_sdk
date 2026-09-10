# PCIe 采集原型（未接入构建）

本目录是为未来 FPGA 光口到 PCIe 图像链路准备的主机侧原型。

**当前状态：不参与 `CMakeLists.txt`，不链接 `pa_host`，不连接 UI，不应作为正式采集程序使用。**

目录内代码的目的：

```text
PcieFrameSource     从未来的 C2H 字符设备读取字节流
PcieFrameParser     在任意 DMA 分块边界下完成帧同步和组帧
Raw12Unpacker       把已经确认格式的 RAW12 转为 uint16 像素
```

它不依赖 `/home/zhe/pcie/pcie` 的驱动源码或 `xray_capture` 实现。旧目录只用于参考 C2H 设备模型、分块读取和帧队列风险。

## 接入前必须确认

以下内容由 FPGA/探测器协议决定，当前代码不会擅自固化：

```text
有效宽高
每行字节数与是否存在行填充
帧头布局、同步字和字段字节序
RAW12 的两个像素三字节打包顺序
是否存在帧尾 CRC / 校验和
帧号溢出和重同步规则
设备节点、DMA 单次读取行为、超时和停止方式
```

确认后应执行：

```text
1. 用协议常量替换 PcieFrameLayout 的运行时配置来源
2. 为 PcieFrameParser 和 Raw12Unpacker 增加固定测试向量
3. 将本目录移动到 src/pcie，并加入 pa_transport 或单独的 pa_acquisition 目标
4. 用 Qt queued signal 将完整帧送到主线程
5. 最后才连接 TiRawImage 和 ImageView
```

实时显示时，消费者落后于采集时应丢弃旧显示帧，只保留最新帧；采集统计必须单独记录协议丢帧和显示丢帧。
