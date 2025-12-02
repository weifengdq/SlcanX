# SlcanX
Four-channel CANFD Analyzer

**Ubuntu20 + Kernel 5.15.x**: u20_k515_slcanx

Notice:

- `slcandx -t hw`, Enabling hardware flow control can't be missing
- In `slcanx.ko` module, setting `tx_batch_us=125` is recommended for better performance.
- Before running the `1_5_x4_slcandx.sh` script, please execute the `clean.sh` script to properly bring down and delete the CAN interfaces when they are no longer needed.
- `sudo sysctl -w net.core.rmem_max=10485760` and `sudo sysctl -w net.core.wmem_max=10485760` can be set for better performance.
- `candump -l -r 1048576 any` can be used to log CAN data to a file with a larger buffer size.