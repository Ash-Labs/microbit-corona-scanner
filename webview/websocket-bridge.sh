#!/bin/sh
if [ -z "$SERIAL_DEVICE" ] 		# SERIAL_DEVICE not set -> invoked by user to start websocketd
then
	if [ $# -eq 1 ]				# check if user supplied arg[1]: serial port device
	then
		if [ -w "$1" ]			# check if device writable (need to set baudrate)
		then
			export SERIAL_DEVICE=$1
			websocketd --passenv SERIAL_DEVICE -port 8090 ./$0
			if [ "$?" -eq 127 ]	# websocketd - command not found
			then
				echo "websocketd not installed"
				echo "debian/ubuntu/etc.: install with 'sudo apt install websocketd'"
				echo "other OS          : see https://github.com/joewalnes/websocketd/"
			fi
		else					# serial device not writable
			echo "serial device $1 not writable! aborting."
		fi
	else						# user didn't supply exactly 1 argument
		echo "usage: $0 /dev/ttyACMx"
	fi
else 							# SERIAL_DEVICE set -> invoked by websocketd to read data from serial port
	stty 115200 -icrnl < $SERIAL_DEVICE
	cat $SERIAL_DEVICE
fi
