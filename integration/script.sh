#!/bin/bash

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
make shared ARCH=$ARCH
cp libtioga.so $RNT_PATH

cd $RNT_PATH
make ARCH=$ARCH

cp $LIB_PATH/queue_test.cpp $RNT_PATH
cp $LIB_PATH/queue_test.h $RNT_PATH
cp $LIB_PATH/../iotypes.h $RNT_PATH
