#!/bin/bash

rm *.so *.o
cp ../../tga_driver/library/tioglib.o .
cp ../../tga_driver/library/queue_test.o .
make static ARCH=arm
