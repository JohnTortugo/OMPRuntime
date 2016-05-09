#!/bin/bash

##if [ -z $ARCH ]
##then
##	ARCH=arm
##fi
##
##if [ $HOSTNAME == "lucas-670Z5E" ]
##then
##	LIB_PATH='/home/lucas/Dropbox/Eclipse_Workspace/tga_driver/library'
##	RNT_PATH='/home/lucas/Dropbox/Eclipse_Workspace/mtsp/integration'
##fi
##
##SCRIPT_DIR=`readlink -f $0`
##
##cd $RNT_PATH
##make clean 
##
##cd $LIB_PATH
##make shared ARCH=$ARCH
##cp libtioga.so $RNT_PATH
##
##cd $RNT_PATH
##make ARCH=$ARCH
##
##cp $LIB_PATH/queue_test.cpp $RNT_PATH
##cp $LIB_PATH/queue_test.h $RNT_PATH
##cp $LIB_PATH/../iotypes.h $RNT_PATH
./script.sh

if [ $NOT_UNICAMP ]
then
	tar c libtioga.so mtsp_bridge.so | ssh -p 6868 lucas.morais@cajarana.lsc.ic.unicamp.br -t "ssh linaro@10.68.30.43 'tar x -C Lukensville/aux_integration_test'"
else
	scp mtsp_bridge.so libtioga.so queue_test.cpp queue_test.h iotypes.h linaro@10.68.30.43:Lukensville/aux_integration_test
fi

rm queue_test.cpp
rm queue_test.h
rm iotypes.h
