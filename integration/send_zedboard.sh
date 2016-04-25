#!/bin/bash

./script.sh

if [ $NOT_UNICAMP ]
then
	tar c libtioga.so mtsp_bridge.so | ssh -p 6868 lucas.morais@cajarana.lsc.ic.unicamp.br -t "ssh linaro@10.68.30.43 'tar x -C Lukensville/integration_test'"
else
	scp mtsp_bridge.so libtioga.so queue_test.cpp queue_test.h iotypes.h linaro@10.68.30.43:Lukensville/integration_test
fi

rm queue_test.cpp
rm queue_test.h
rm iotypes.h
