# SLCANX Interface for python-can

This package provides a `python-can` interface for the SLCANX protocol, which supports 4 CAN channels over a single USB CDC ACM serial port.

## Installation

```bash
pip install .
```

## Usage

```python
import can

# Open channel 0 on COM3
bus0 = can.Bus(interface='slcanx', channel='COM3', slcanx_channel=0, bitrate=500000)

# Open channel 1 on COM3 (shares the same serial port)
bus1 = can.Bus(interface='slcanx', channel='COM3', slcanx_channel=1, bitrate=500000)

msg = can.Message(arbitration_id=0x123, data=[0, 1, 2, 3, 4, 5, 6, 7], is_extended_id=False)
bus0.send(msg)

for msg in bus0:
    print(msg)
```

## Examples

01_simple_std.py, the id 0x123 message is sent once

```bash
> python3 .\examples\01_simple_std.py --port COM99 --channel 0 --bitrate 500000 
Opening COM99 Channel 0 @ 500000bps...
Sending: Timestamp:        0.000000    ID:      123    S Rx                DL:  4    11 22 33 44
Listening for messages (Ctrl+C to exit)...
Received: Timestamp: 1764903904.070060    ID:      456    S Rx                DL:  8    12 34 56 78 90 ab cd ef
Received: Timestamp: 1764903905.071258    ID:      456    S Rx                DL:  8    13 34 56 78 90 ab cd ef
Received: Timestamp: 1764903906.072802    ID:      456    S Rx                DL:  8    14 34 56 78 90 ab cd ef
Received: Timestamp: 1764903907.073035    ID:      456    S Rx                DL:  8    15 34 56 78 90 ab cd ef
Received: Timestamp: 1764903908.073823    ID:      456    S Rx                DL:  8    16 34 56 78 90 ab cd ef
Received: Timestamp: 1764903909.074551    ID:      456    S Rx                DL:  8    17 34 56 78 90 ab cd ef
Received: Timestamp: 1764903910.075336    ID:      456    S Rx                DL:  8    18 34 56 78 90 ab cd ef
```

02_periodic.py, the id 0x100 message is sent every second

```bash
> python3 .\examples\02_periodic.py --port COM99 --channel 0 --bitrate 500000   
Opening COM99 Channel 0 @ 500000bps...
Starting periodic send of Timestamp:        0.000000    ID:      100    S Rx                DL:  4    00 01 02 03 every 1s
Listening... (Ctrl+C to exit)
Received: Timestamp: 1764904316.414301    ID:      456    S Rx                DL:  8    12 34 56 78 90 ab cd ef
Received: Timestamp: 1764904320.011331    ID:      456    S Rx                DL:  8    12 34 56 78 90 ab cd ef
Received: Timestamp: 1764904321.012076    ID:      456    S Rx                DL:  8    13 34 56 78 90 ab cd ef
Received: Timestamp: 1764904322.012147    ID:      456    S Rx                DL:  8    14 34 56 78 90 ab cd ef
Received: Timestamp: 1764904323.013043    ID:      456    S Rx                DL:  8    15 34 56 78 90 ab cd ef
```

03_multi_std_threading.py, open all 4 channels and listen/send in separate threads

```bash
> python3 .\examples\03_multi_std_threading.py --port COM99 --bitrate 1000000            
Opening Channel 0...
[Ch0] Listener started
[Ch0] Transmitter started
Opening Channel 1...
[Ch1] Listener started
[Ch1] Transmitter started
Opening Channel 2...
[Ch2] Listener started
[Ch2] Transmitter started
Opening Channel 3...
[Ch3] Listener started
[Ch3] Transmitter started
All channels running. Ctrl+C to exit.
[Ch0] Rx: Timestamp: 1764904966.076559    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
[Ch1] Rx: Timestamp: 1764904978.109060    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
[Ch2] Rx: Timestamp: 1764904981.152110    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
[Ch3] Rx: Timestamp: 1764904983.789484    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
```

04_multi_std_asyncio.py, open all 4 channels and listen/send using asyncio

```bash
> python3 .\examples\04_multi_std_asyncio.py --port COM99 --bitrate 1000000   
Opening Channel 0...
Opening Channel 1...
Opening Channel 2...
Opening Channel 3...
All channels running. Ctrl+C to exit.
[Ch0] Handler started
[Ch1] Handler started
[Ch2] Handler started
[Ch3] Handler started
[Ch0] Rx: Timestamp: 1764905065.098601    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
[Ch1] Rx: Timestamp: 1764905067.721127    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
[Ch2] Rx: Timestamp: 1764905071.702690    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
[Ch3] Rx: Timestamp: 1764905074.416996    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
```

05_simple_fd.py, sending and receiving CAN FD frames, 500K 80% + 2M 80%

```bash
> python3 .\examples\05_simple_fd.py --port COM99 --channel 0                   
Opening COM99 Channel 0 FD
Arb: 500000bps (80%), Data: 2000000bps (80%)
Sending FD frame (64 bytes)...
Listening...
Received: Timestamp: 1764905265.839866    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
Received: Timestamp: 1764905266.299860    ID: 00000002    X Rx                DL:  8    11 00 00 00 00 00 00 ff
Received: Timestamp: 1764905266.799620    ID:      123    S Rx   R            DL:  8
Received: Timestamp: 1764905267.346174    ID: 12345678    X Rx   R            DL:  8
Received: Timestamp: 1764905268.224543    ID:      7f0    S Rx     F          DL: 12    11 00 00 00 00 00 00 00 00 00 00 ff
Received: Timestamp: 1764905269.317547    ID: 1ffffff0    X Rx     F          DL: 12    11 00 00 00 00 00 00 00 00 00 00 ff
Received: Timestamp: 1764905270.884201    ID:      000    S Rx     F BS       DL:  1    11
Received: Timestamp: 1764905272.878433    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
```

06_multi_fd_threading.py, open all 4 channels with CAN FD and listen/send in separate threads, 1M 80% + 8M 80%

```bash
> python3 .\examples\06_multi_fd_threading.py --port COM99
Opening Channel 0 FD...
[Ch0] Listener started
[Ch0] Transmitter started
Opening Channel 1 FD...
[Ch1] Listener started
[Ch1] Transmitter started
Opening Channel 2 FD...
[Ch2] Listener started
[Ch2] Transmitter started
Opening Channel 3 FD...
[Ch3] Listener started
[Ch3] Transmitter started
All channels running. Ctrl+C to exit.
[Ch0] Rx: Timestamp: 1764905447.070846    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch1] Rx: Timestamp: 1764905450.126225    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch3] Rx: Timestamp: 1764905458.811920    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch2] Rx: Timestamp: 1764905484.033054    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
```

07_multi_fd_asyncio.py, open all 4 channels with CAN FD and listen/send using asyncio, 1M 80% + 8M 80%

```bash
> python3 .\examples\07_multi_fd_asyncio.py --port COM99  
Opening Channel 0 FD...
Opening Channel 1 FD...
Opening Channel 2 FD...
Opening Channel 3 FD...
All channels running. Ctrl+C to exit.
[Ch0] Handler started
[Ch1] Handler started
[Ch2] Handler started
[Ch3] Handler started
[Ch3] Rx: Timestamp: 1764905613.141724    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch1] Rx: Timestamp: 1764905620.115503    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch0] Rx: Timestamp: 1764905622.842793    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch2] Rx: Timestamp: 1764905643.699782    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
```

08_custom_timing.py, applying custom CAN FD timing settings, 1M 80% + 8M 80%

```bash
> python3 .\examples\08_custom_timing.py --port COM99 --channel 0
Opening COM99 Channel 0...
Applying custom timing: a80_2_31_8_8_0, A80_1_7_2_2_1
Sending custom timing frame...
Listening...
Received: Timestamp: 1764905851.506521    ID:      001    S Rx                DL:  8    11 00 00 00 00 00 00 ff
Received: Timestamp: 1764905851.891661    ID: 00000002    X Rx                DL:  8    11 00 00 00 00 00 00 ff
Received: Timestamp: 1764905852.408906    ID:      123    S Rx   R            DL:  8
Received: Timestamp: 1764905852.983500    ID: 12345678    X Rx   R            DL:  8
Received: Timestamp: 1764905853.492063    ID:      7f0    S Rx     F          DL: 12    11 00 00 00 00 00 00 00 00 00 00 ff
Received: Timestamp: 1764905854.029209    ID: 1ffffff0    X Rx     F          DL: 12    11 00 00 00 00 00 00 00 00 00 00 ff
Received: Timestamp: 1764905854.512155    ID:      000    S Rx     F BS       DL:  1    11
Received: Timestamp: 1764905862.855249    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
```

09_multi_custom_timing_threading.py, open all 4 channels with custom CAN FD timing and listen/send in separate threads, 1M 80% + 8M 80%

```bash
> python3 .\examples\09_multi_custom_timing_threading.py --port COM99
Opening Channel 0 Custom Timing...
[Ch0] Listener started
[Ch0] Transmitter started
Opening Channel 1 Custom Timing...
[Ch1] Listener started
[Ch1] Transmitter started
Opening Channel 2 Custom Timing...
[Ch2] Listener started
[Ch2] Transmitter started
Opening Channel 3 Custom Timing...
[Ch3] Listener started
[Ch3] Transmitter started
All channels running. Ctrl+C to exit.
[Ch0] Rx: Timestamp: 1764906041.286532    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch1] Rx: Timestamp: 1764906044.148943    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch2] Rx: Timestamp: 1764906061.652194    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch3] Rx: Timestamp: 1764906071.220882    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
```

10_multi_custom_timing_asyncio.py, open all 4 channels with custom CAN FD timing and listen/send using asyncio, 1M 80% + 8M 80%


```bash
> python3 .\examples\10_multi_custom_timing_asyncio.py --port COM99  
Opening Channel 0 Custom Timing...
Opening Channel 1 Custom Timing...
Opening Channel 2 Custom Timing...
Opening Channel 3 Custom Timing...
All channels running. Ctrl+C to exit.
[Ch0] Handler started
[Ch1] Handler started
[Ch2] Handler started
[Ch3] Handler started
[Ch3] Rx: Timestamp: 1764912176.313900    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch1] Rx: Timestamp: 1764912184.084967    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch0] Rx: Timestamp: 1764912194.235446    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
[Ch2] Rx: Timestamp: 1764912238.296757    ID: 1fffffff    X Rx     F BS       DL: 64    11 22 33 44 55 66 77 88 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff
```