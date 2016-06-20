#ifndef NEXUS_H
#define NEXUS_H

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
System level includes
-----------------------------------------------------------------------------*/
#include <asm/ioctl.h>

/*-----------------------------------------------------------------------------
Public Defines
-----------------------------------------------------------------------------*/
#define NEXUS_NAME            "nexusIO_ddr"
#define NEXUS_DEV             "/dev/nexusIO_ddr"
/*-----------------------------------------------------------------------------
IOCTL Defines
-----------------------------------------------------------------------------*/
#define NEXUS_IOC_MAGIC 0xEE
#define NEXUS_IOC_HWADDR _IOR(NEXUS_IOC_MAGIC, 0, unsigned long int *)
#define NEXUS_IOC_LENGTH _IOR(NEXUS_IOC_MAGIC, 1, unsigned long int *)
#define NEXUS_IOC_MAXNR   _IO(NEXUS_IOC_MAGIC, 2)

/*-----------------------------------------------------------------------------
Public Data Types
-----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* RESMEM_H */
