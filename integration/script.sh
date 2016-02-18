#!/bin/bash

rm *.so *.o
cp ../../SchedulerModel/libtga.so .
cp ../../tga_driver/library/libtioga.so .
make
