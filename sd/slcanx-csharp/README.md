# slcanx-csharp

C# library and examples for the SLCANX protocol (4-channel CAN FD over USB CDC).

## Features

- Supports 4 channels (0-3) sharing a single serial port.
- CAN 2.0 and CAN FD support.
- Write grouping optimization: Buffers frames for up to 125us to improve USB throughput.
- Thread-safe API.
- Examples for threading, async/await, and custom timing.

## Usage

```csharp
using Slcanx;

// Connect to COM port
using (var slcanx = new Slcanx("COM3"))
{
    slcanx.Open();
    
    // Get channel 0
    var ch0 = slcanx.GetChannel(0);
    
    // Configure
    ch0.SetRate(6); // 500kbps
    ch0.Open();
    
    // Send
    ch0.Send(new CanFrame(0x123, new byte[] { 1, 2, 3 }));
    
    // Receive
    ch0.FrameReceived += (frame) => {
        Console.WriteLine($"RX: {frame}");
    };
    
    Console.ReadLine();
}
```

## Examples

1. `01_SimpleStd`: Basic send/receive.
2. `02_Periodic`: Periodic sending using Timer.
3. `03_MultiStdThreading`: Multiple channels with threads.
4. `04_MultiStdAsync`: Multiple channels with async/await.
5. `05_SimpleFd`: CAN FD example.
6. `06_MultiFdThreading`: CAN FD with threads.
7. `07_MultiFdAsync`: CAN FD with async.
8. `08_CustomTiming`: Custom bit timing.
9. `09_MultiCustomTimingThreading`: Custom timing with threads.
10. `10_MultiCustomTimingAsync`: Custom timing with async.
11. `11_SimpleStdRxTx`: Simple Standard Frame Receive and Transmit.
12. `12_MultiCustomTimingAsyncRxTx`: Multi-channel Custom Timing Async Receive and Transmit.

## Build

```bash
dotnet build
```

## Run Example

```bash
dotnet run --project Examples/01_SimpleStd COM3
```
