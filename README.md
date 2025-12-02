# SlcanX
Four-channel CANFD Analyzer

Introduction to the file or folders:
- `u20_k515_slcanx/`: SlcanX driver and scripts for Ubuntu 20.04 with Kernel 5.15.x
- `u22_k68_slcanx/`: SlcanX driver and scripts for Ubuntu 22.04 with Kernel 6.8.x


Recovery:
- Unplug the USB of the SLCANX device
- `clean.sh` to remove CAN interfaces and unload the module
- `1_5_x4_slcandx.sh` to load the module and create CAN interfaces

Notice:

- `slcandx -t hw`, Enabling hardware flow control can't be missing in `1_5_x4_slcandx.sh`
- In `slcanx.ko` module, setting `tx_batch_us=125` is recommended for better performance.
- Before running the `1_5_x4_slcandx.sh` script, please execute the `clean.sh` script to properly bring down and delete the CAN interfaces when they are no longer needed.
- `sudo sysctl -w net.core.rmem_max=10485760` and `sudo sysctl -w net.core.wmem_max=10485760` can be set for better performance.
- `candump -l -r 1048576 any` can be used to log CAN data to a file with a larger buffer size.
- `ip -d -s link show can0` can be used to display detailed statistics of the CAN interface.

Send Tests:
- `cangen can0 -g 0.002 -I 555 -L 0 -b` in 2 terminal windows for sending CAN frames at 1Mbps + 5Mbps