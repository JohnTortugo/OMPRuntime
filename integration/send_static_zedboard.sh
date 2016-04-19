#!/bin/bash

make clean
cp ../../tga_driver/library/tioglib.o .
cp ../../tga_driver/library/queue_test.o .
make static ARCH=arm
scp libmtsp.a linaro@10.68.30.43:Lukensville/integration_test
