#!/bin/bash

tty_device="/dev/ttyACM0"
can_ifs=(0 1 2 3)
txqueuelength=10000
# CLK_PRE_SEG1_SEG2_SJW_TDC
nominal=(80_2_31_8_8_0 80_2_31_8_8_0 80_2_31_8_8_0 80_2_31_8_8_0)
data=(80_1_7_2_2_1 80_1_7_2_2_1 80_1_7_2_2_1 80_1_7_2_2_1)

for i in "${can_ifs[@]}"; do
	if ip link show can$i &> /dev/null; then
		echo "Bringing down and deleting interface can$i"
		sudo ip link set down can$i
		sudo ip link delete can$i
	fi
done

if pgrep slcandx &> /dev/null; then
	echo "Killing slcandx processes"
	sudo pkill slcandx
  sleep 1
  sudo ./slcandx/slcandx -t hw -0c -1c -2c -3c $tty_device
  sudo pkill slcandx
  sleep 1
fi

if lsmod | grep -q slcanx; then
	echo "Removing slcanx module"
	sudo rmmod slcanx
fi

sudo modprobe can
sudo modprobe can-raw
sudo modprobe can-dev
sudo insmod ./slcanx_module/slcanx.ko tx_batch_us=125

sudo ./slcandx/slcandx -t hw \
	-0c -0a${nominal[0]} -0A${data[0]} -0o \
  -1c -1a${nominal[0]} -1A${data[0]} -1o \
  -2c -2a${nominal[0]} -2A${data[0]} -2o \
  -3c -3a${nominal[0]} -3A${data[0]} -3o \
  $tty_device

for i in "${can_ifs[@]}"; do
  sudo ip link set up can$i
  sudo ip link set txqueuelen $txqueuelength can$i
done