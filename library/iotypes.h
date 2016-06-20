/********************************************************
 * Author : Eduardo F.                                  *
 * File   : iotypes.h                                   *
 *                                                      *
 * This file defines the codes to be used as commands   *
 * through ioctl function as well as communication data *
 * structures. This header files should be included by  *
 * both the driver and the API for the communication to *
 * match.                                               *
 *******************************************************/

#ifndef __IOCODES_H__
#define __IOCODES_H__

#include <asm/ioctl.h>

//struct used to pass addresses for the base registers initialization
typedef struct init_s {
    void *USBase;         //user space base address
    void *subQ;           //submission queue address
    void *runQ[4];        //run queue address
    int  flags;           //flags to set control register
} init_t;

#define MAGIC 0xEE		                        //magic number definition
//Defined codes
#define INIT         _IO (MAGIC,  0)                        //init function
#define SIG          _IOW (MAGIC,  1, int)                  //set signature
#define SETSTRUCT    _IOW (MAGIC,  2, int)                  //set all internal structures
#define UBASE        _IOW (MAGIC,  3, void*)                //initialize user space base address register
#define SUBBASE      _IOW (MAGIC,  4, void*)                //initialize submission queue base address register
#define RUN0BASE     _IOW (MAGIC,  5, void*)                //initialize run queue 0 base address register
#define RUN1BASE     _IOW (MAGIC,  6, void*)                //initialize run queue 1 base address register
#define RUN2BASE     _IOW (MAGIC,  7, void*)                //initialize run queue 2 base address register
#define RUN3BASE     _IOW (MAGIC,  8, void*)                //initialize run queue 3 base address register
#define CTXBUFF      _IOW (MAGIC,  9, void*)                //initialize state buffer base address register
#define CTRL0        _IOW (MAGIC, 10, int)	                //set control register 0
#define CTRL1        _IOW (MAGIC, 11, int)                  //set control register 1
#define FINISH       _IO  (MAGIC, 12)                       //finish function
#define REGREAD      _IOR (MAGIC, 13, int)                  //read register specified in ioctl parameter
#define DUMPREGS     _IOR (MAGIC, 14, int)                  //read all registers and dump them
#define RESETREGS    _IOW (MAGIC, 15, int)                  //reset all register writing zero to them

//Definition for register read in ioctl
#define REG_USBASE       0
#define REG_SUBBASE      1
#define REG_RUN0BASE     2
#define REG_RUN1BASE     3
#define REG_RUN2BASE     4
#define REG_RUN3BASE     5
#define REG_SIG          6
#define REG_CTRL0        7
#define REG_CTRL1        8
#define REG_STATEBUFF    9

#endif
