#!/bin/bash

make clean
cp ../../SchedulerModel/libtga.so .
cp ../../tga_driver/library/libtioga.so .
make ARCH=arm
scp mtsp_bridge.so libtioga.so linaro@10.68.30.43:Lukensville/integration_test
