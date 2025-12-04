#!/bin/bash

# Flush all existing rules
sudo cangw -F

# 64 bytes of zero data (128 hex characters)
DATA="00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"

# 1. Forward CAN FD frames from can0, can1, can2 to can3
# -X: CAN FD rule
# -M: Modify CAN FD frame
# SET:F: Set Flags
# 0.01.0.$DATA -> ID=0, Flags=0x01 (BRS), Len=0, Data=...
for dev in can0 can1 can2; do
    sudo cangw -A -s $dev -d can3 -e -X -M SET:F:0.01.0.$DATA
done

# 2. Forward Classical CAN frames and convert to CAN FD with BRS
# Since cangw cannot convert Classical CAN to CAN FD, we use a user-space bridge.
# We filter out CAN FD frames (containing '##') to avoid duplication.

# Stop previous background jobs if any
pkill -f "candump -L can0 can1 can2" 2>/dev/null

# Start the bridge in background
# candump -L outputs: (timestamp) interface ID#Data
# We convert ID#Data to ID##1Data (CAN FD with BRS)
nohup bash -c 'stdbuf -oL candump -L can0 can1 can2 | grep --line-buffered -v "##" | while read line; do
    frame=$(echo "$line" | awk "{print \$3}")
    # Replace first # with ##1
    new_frame=${frame/\#/##1}
    cansend can3 "$new_frame"
done' >/dev/null 2>&1 &

echo "Rules applied. User-space bridge started for Classical CAN -> CAN FD conversion."
