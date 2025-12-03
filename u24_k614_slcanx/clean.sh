#!/bin/bash

for i in 0 1 2 3; do
	if ip link show can$i &> /dev/null; then
		# 打印
		echo "Bringing down and deleting interface can$i"
		sudo ip link set down can$i
		sudo ip link delete can$i
	fi
done

if pgrep slcandx &> /dev/null; then
	echo "Killing slcandx processes"
	sudo pkill slcandx
	# sudo ./slcandx/slcandx clean
fi

sudo rmmod slcan
sudo rmmod slcanfd
if lsmod | grep -q slcanx; then
	echo "Removing slcanx module"
	sudo rmmod slcanx
fi
