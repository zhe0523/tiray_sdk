# Changelog

## 未发布

- 增加依赖 SDK 公共 API 的维护上位机 `tools/tiray_host`（构建开关 `-DTIRAY_SDK_BUILD_HOST=ON`，目标 `tiray_host`），用于维护和 SDK 回归；不合并研发自有协议栈 `pa_host` / `pa_controller`。

## 0.1.0

- 冻结 C99 业务 API 和 C++17 RAII API；
- 增加 RS422 二进制通信、超时重试和异步 PING；
- 增加 Static、Dynamic、配置组和设备重启接口；
- 增加高层 Static/Dynamic 配置结构；
- 增加暗场、亮场模板制作和模板上传查询；
- 增加 PCIe 图像接收、BAR0 自动发现和 `image_type` 解析；
- 增加客户文档目录 `docs/customer/`（与内部文档分离）和交互式硬件测试程序。
- 客户安装包不再包含研发 README 与内部构建说明。
