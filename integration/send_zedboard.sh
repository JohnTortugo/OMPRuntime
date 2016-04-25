#!/bin/bash

./script.sh

if [ $NOT_UNICAMP ]
then
	tar c libtioga.so mtsp_bridge.so | ssh -p 6868 lucas.morais@cajarana.lsc.ic.unicamp.br -t "ssh linaro@10.68.30.43 'tar x -C Lukensville/integration_test'"
else
	scp mtsp_bridge.so libtioga.so linaro@10.68.30.43:Lukensville/integration_test
fi
