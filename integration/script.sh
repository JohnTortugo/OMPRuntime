#!/bin/bash

(rm mem_interface.* || true) 2> /dev/null

if [ -z $ARCH ]
then
	ARCH=arm
fi

if [ $HOSTNAME == "lucas-670Z5E" ]
then
	LIB_PATH='/home/lucas/Dropbox/Eclipse_Workspace/tga_driver/library'
	RNT_PATH='/home/lucas/Dropbox/Eclipse_Workspace/mtsp/integration'
fi

SCRIPT_DIR=`readlink -f $0`

cd $RNT_PATH
make clean 

cd $LIB_PATH
make shared
cp libtioga.so $RNT_PATH

cd $RNT_PATH
make ARCH=$ARCH

cp $LIB_PATH/mem_interface.cpp $RNT_PATH
cp $LIB_PATH/mem_interface.h $RNT_PATH
cp $LIB_PATH/../iotypes.h $RNT_PATH
