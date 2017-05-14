#!/usr/bin/env bash

#Fatal One!
touch user/user_main.c

make COMPILE=gcc BOOT=new APP=1 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=4

if [ $? -eq 0 ] ; then
	ls -lh ../bin/upgrade | grep .bin
	ls -l ../bin/upgrade | grep .bin
	echo "Sucessfully!"
	exit 0
else
	echo "Something wrong!"
fi
