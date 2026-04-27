#!/bin/bash

if pgrep slcandx &> /dev/null; then
	echo "Killing slcandx processes"
	sudo pkill slcandx
	sleep 1
	# sudo ./slcandx/slcandx clean
fi

sudo rmmod slcan
sudo rmmod slcanfd
if lsmod | grep -q slcanx; then
	echo "Removing slcanx module"
	sudo rmmod slcanx
fi
