#!/bin/bash

# sudo insmod ./slcanx_module/slcanx.ko
sudo insmod ./slcanx_module/slcanx.ko tx_batch_us=125

sudo ./slcandx/slcandx -t hw \
	-0c -0y1000000 -0Y5 -0p800 -0P750 -0o \
	-1c -1y1000000 -1Y5 -1p800 -1P750 -1o \
	-2c -2y1000000 -2Y5 -2p800 -2P750 -2o \
	-3c -3y1000000 -3Y5 -3p800 -3P750 -3o \
	/dev/ttyACM0

for i in 0 1 2 3; do
	sudo ip link set up can$i
	sudo ifconfig can$i txqueuelen 1000
done