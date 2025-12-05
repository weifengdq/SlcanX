# SLCANX C++ Library

A C++17 library for the SLCANX protocol (4-channel CAN/CAN FD over USB CDC).

## Features

- **Multi-channel**: Supports 4 CAN channels.
- **CAN FD**: Full support for CAN FD and Bit Rate Switching (BRS).
- **Performance**: Implements write grouping (default 125us window) to optimize USB throughput.
- **Thread-safe**: Safe for multi-threaded use.
- **Callbacks**: Asynchronous reception via callbacks.

## Build

Requires CMake 3.10+ and a C++17 compiler.

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Usage

```cpp
#include "slcanx.hpp"

// Initialize on COM3
slcanx::Slcanx bus("COM3");

// Configure Channel 0
bus.set_bitrate(0, 500000);
bus.open_channel(0);

// Set callback
bus.set_rx_callback([](uint8_t ch, const slcanx::CanFrame& frame) {
    std::cout << "Received on Ch" << (int)ch << std::endl;
});

// Send
std::vector<uint8_t> data = {1, 2, 3};
auto frame = slcanx::CanFrame::new_std(0x123, data);
bus.send(0, frame);
```

## Examples

- `01_simple_std`: Single channel standard CAN.
- `02_periodic`: Periodic sending.
- `03_multi_std_threading`: 4-channel concurrent sending.
- `05_simple_fd`: CAN FD usage.
- `08_custom_timing`: Custom bit timing configuration.
