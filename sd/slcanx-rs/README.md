### 示例代码

在 `examples/` 目录下提供了以下示例（对应 Python 版本）：

*   `01_simple_std.rs`: 单通道标准帧收发。
*   `02_periodic.rs`: 单通道周期性发送。
*   `03_multi_std_threading.rs`: 4 通道多线程并发发送。
*   `05_simple_fd.rs`: 单通道 CAN FD 收发（500K/2M）。
*   `08_custom_timing.rs`: 自定义位时间参数配置。

### 运行方法

确保已安装 Rust 工具链，然后在 slcan-rs 目录下运行：

```bash
# 运行单通道标准帧示例
cargo run --example 01_simple_std -- --port COM3

# 运行 4 通道多线程示例
cargo run --example 03_multi_std_threading -- --port COM3

# 运行自定义时序示例
cargo run --example 08_custom_timing -- --port COM3
```

