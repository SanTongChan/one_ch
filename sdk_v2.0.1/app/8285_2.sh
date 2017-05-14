#!/usr/bin/env bash

# Fatal One !
touch user/user_main.c

make COMPILE=gcc BOOT=new APP=2 SPI_SPEED=40 SPI_MODE=DOUT SPI_SIZE_MAP=2

if [ $? -eq 0 ] ; then
	ls -lh ../bin/upgrade | grep .bin
	ls -l ../bin/upgrade | grep .bin
	echo "Successfully!"
	exit 0
else
	echo "Something wrong!"
fi

