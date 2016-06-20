/********************************************************
 * Author: Eduardo F.                                   *
 * File:   mtypes.h                                     *
 *                                                      *
 * This file defines the types used along this driver   *
 * creation.                                            *
 *******************************************************/

#ifndef __MTYPES_H__
#define __MTYPES_H__

#include <linux/types.h>
#include <linux/kernel.h>

#define TRUE  1
#define FALSE 0

#define SUCCESS  0

//Preprocessor function for returning base address + ofset
#define add(offset) device_mem + offset * 4

//Definitions of registers offset, in words
#define HWS_CONTROL	    0x0
#define HWS_STATUS	    0x1
#define DOORBELL_L	    0x4
#define DOORBELL_H	    0x5
#define LASTMSG_L	    0x6
#define LASTMSG_H	    0x7
#define UINTR		    0x8
#define KINTR		    0x9
#define UIEN		    0xa
#define KIEN		    0xb
#define VER_L		    0xe
#define VER_H		    0xf
#define FINTASK_L	    0x10
#define FINTASK_H	    0x11
#define FSM_NOT_IDLE	0x1c
#define HWERR		    0x1e
#define AW_MST_PROP	    0x618
#define AR_MST_PROP	    0x619
#define ACE_MST_CONT	0x612
#define AX_SUBM_Q	    0x614
#define AX_RUN_Q	    0x615
#define RUN_Q_0_L	    0x600
#define RUN_Q_0_H	    0x601
#define RUN_Q_1_L	    0x602
#define RUN_Q_1_H	    0x603
#define RUN_Q_2_L	    0x604
#define RUN_Q_2_H	    0x605
#define RUN_Q_3_L	    0x606
#define RUN_Q_3_H	    0x607
#define SUB_Q_L		    0x608
#define SUB_Q_H		    0x609
#define TLB_0_ENTRY_L	0X620
#define TLB_0_ENTRY_H	0X621
#define TLB_1_ENTRY_L	0X622
#define TLB_1_ENTRY_H	0x623
#define TLB_2_ENTRY_L	0x624
#define TLB_2_ENTRY_H	0x625
#define TLB_3_ENTRY_L	0x626
#define TLB_3_ENTRY_H	0x627
#define TLB_4_ENTRY_L	0x628
#define TLB_4_ENTRY_H	0x629
#define TLB_5_ENTRY_L	0x62a
#define TLB_5_ENTRY_H	0x62b
#define TLB_0_MAP_L		0x630
#define TLB_0_MAP_H		0x631
#define TLB_1_MAP_L		0x632
#define TLB_1_MAP_H		0x633
#define TLB_2_MAP_L		0x634
#define TLB_2_MAP_H		0x635
#define TLB_3_MAP_L		0x636
#define TLB_3_MAP_H		0x637
#define TLB_4_MAP_L		0x638
#define TLB_4_MAP_H		0x639
#define TLB_5_MAP_L		0x63a
#define TLB_5_MAP_H		0x63b
#define CYCLE_CNT_L		0x24e
#define CYCLE_CNT_H		0x24f
#define STAT_CTL        0x25e
#define ACE_MST_CON		0x612

//Error codes definition
#define ERR_PTR           1        //received pointer is NULL
#define ERR_RST           2        //trying to start device when already started
#define ERR_SIG           3        //call to init_signature set failed
#define ERR_DBVAL         4        //doorbell ring value is invalid (== 0)
#define ERR_DB            5        //call to op_DBRring failed
#define ERR_LMC           6        //error due to invalid value (zero) found on the last message register
#define ERR_REQ           7        //call to write finished task to register failed
#define ERR_CTRL0         8        //call to control_reg0 failed
#define ERR_CTRL1         9        //call to control_reg1 failed
#define ERR_FIN          10        //call to fin_end failed

#endif
