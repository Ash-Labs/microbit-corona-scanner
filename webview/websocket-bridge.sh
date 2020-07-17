#!/bin/sh
if [ -z "$SERIAL_DEVICE" ]
then
	if [ $# -eq 1 ]
	then
		export SERIAL_DEVICE=$1
		websocketd --passenv SERIAL_DEVICE -port 8090 ./$0
		if [ "$?" -eq 127 ]
		then
			echo "websocketd not installed"
			echo "debian/ubuntu/etc.: install with 'sudo apt install websocketd'"
			echo "other OS          : see https://github.com/joewalnes/websocketd/"
		fi
	else
		echo "usage: $0 /dev/ttyACMx"
	fi
else
	stty 115200 -icrnl < $SERIAL_DEVICE
	cat $SERIAL_DEVICE
fi
