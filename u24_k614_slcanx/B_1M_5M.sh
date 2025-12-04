#!/bin/bash

tty_device="/dev/ttyACM0"
can_ifs=(0 1 2 3)
nominal_bitrate=(1000000 1000000 1000000 1000000)
nominal_sample_point=(800 800 800 800)
data_bitrate=(5 5 5 5)
data_sample_point=(750 750 750 750)
txqueuelength=10000

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
	-0c -0y${nominal_bitrate[0]} -0Y${data_bitrate[0]} -0p${nominal_sample_point[0]} -0P${data_sample_point[0]} -0o \
  -1c -1y${nominal_bitrate[1]} -1Y${data_bitrate[1]} -1p${nominal_sample_point[1]} -1P${data_sample_point[1]} -1o \
  -2c -2y${nominal_bitrate[2]} -2Y${data_bitrate[2]} -2p${nominal_sample_point[2]} -2P${data_sample_point[2]} -2o \
  -3c -3y${nominal_bitrate[3]} -3Y${data_bitrate[3]} -3p${nominal_sample_point[3]} -3P${data_sample_point[3]} -3o \
  $tty_device

for i in "${can_ifs[@]}"; do
  sudo ip link set up can$i
  sudo ip link set txqueuelen $txqueuelength can$i
done