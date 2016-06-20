#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <asm/uaccess.h>
#include "nexusIO.h"

#define SUCCESS 0

/*-----------------------------------------------------------------------------
Private data
-----------------------------------------------------------------------------*/
MODULE_LICENSE("GPL v2");

static char driver_name[] = "nexusIO";

static unsigned long nexusIO_hwaddr = 0;
static unsigned long nexusIO_length = 0;

module_param(nexusIO_hwaddr, ulong, S_IRUSR);
module_param(nexusIO_length, ulong, S_IRUSR);

static int occupied = 0;

/*-----------------------------------------------------------------------------
file operations
-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
NAME
    nexusIO_open

DESCRIPTION
    open file operation for module device file

RETURNS
    SUCCESS or error code

-----------------------------------------------------------------------------*/
static int nexusIO_open(struct inode *inode, struct file *filp)
{
    printk("%s: opened\n", driver_name);

    if (occupied)
    {
        return -EBUSY;
    }
    occupied = 1;

    return SUCCESS;
}

/*-----------------------------------------------------------------------------
NAME
    nexusIO_release

DESCRIPTION
    release file operation for module device file

RETURNS
    SUCCESS or error code

-----------------------------------------------------------------------------*/
static int nexusIO_release(struct inode *inode, struct file *filp)
{
    occupied = 0;

    printk("%s: released\n", driver_name);

    return SUCCESS;
}

/*-----------------------------------------------------------------------------
NAME
    nexusIO_mmap

DESCRIPTION
    mmap file operation for module device file

RETURNS
    SUCCESS or error code

-----------------------------------------------------------------------------*/
static int nexusIO_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int result;
    unsigned long requested_size;

    printk("%s: mmap...\n", driver_name);

    requested_size = vma->vm_end - vma->vm_start;
    if (requested_size != nexusIO_length)
    {
        printk(KERN_ERR "%s: Error: %lu reserved != %lu requested)\n",
            driver_name, nexusIO_length, requested_size);
        return -EAGAIN;
    }
    
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot); 
    vma->vm_page_prot = pgprot_writecombine( vma->vm_page_prot);
    //vma->vm_flags |= VM_IO;
    //vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);
    vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
    
    result = io_remap_pfn_range(vma, vma->vm_start,
        nexusIO_hwaddr >> PAGE_SHIFT,
        nexusIO_length, vma->vm_page_prot);
    if (result)
    {
        printk(KERN_ERR "%s: Error in calling remap_pfn_range: returned %d\n",
            driver_name, result);
        return -EAGAIN;
    }

    printk("%s: mmap OK\n", driver_name);
    return SUCCESS;
}


/*-----------------------------------------------------------------------------
NAME
    nexusIO_ioctl

DESCRIPTION
    ioctl file operation for module device file

RETURNS
    SUCCESS or error code

-----------------------------------------------------------------------------*/
static long nexusIO_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    printk("%s: ioctl: cmd = %x\n", driver_name, cmd);

    if (_IOC_TYPE(cmd) != NEXUS_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > NEXUS_IOC_MAXNR) return -ENOTTY;

    switch(cmd)
    {
        case NEXUS_IOC_HWADDR:
        {
            printk("%s: ioctl: NEXUS_IOC_HWADDR\n", driver_name);

            if (copy_to_user((void*)arg, &nexusIO_hwaddr, sizeof(nexusIO_hwaddr)))
            {
                printk("%s: ioctl: NEXUS_IOC_HWADDR: failed to copy to user\n", driver_name);
                return -EFAULT;
            }
            return SUCCESS;
        }

        case NEXUS_IOC_LENGTH:
        {
            printk("%s: ioctl: NEXUS_IOC_LENGTH\n", driver_name);

            if (copy_to_user((void*)arg, &nexusIO_length, sizeof(nexusIO_length)))
            {
                printk("%s: ioctl: NEXUS_IOC_LENGTH: failed to copy to user\n", driver_name);
                return -EFAULT;
            }
            return SUCCESS;
        }

        default:
            return -ENOTTY;
    }

    return -ENOTTY;
}


/*-----------------------------------------------------------------------------
 file operations and device definition
-----------------------------------------------------------------------------*/

static const struct file_operations nexusIO_fops = {
    owner:      THIS_MODULE,
    unlocked_ioctl:      nexusIO_ioctl,
    open:       nexusIO_open,
    mmap:       nexusIO_mmap,
    release:    nexusIO_release,
};

static struct miscdevice nexusIO_dev = {
        MISC_DYNAMIC_MINOR,
        NEXUS_NAME,
        &nexusIO_fops
};


/*-----------------------------------------------------------------------------
NAME
    nexusIO_init

DESCRIPTION
    mandatory module exit function

RETURNS
    SUCCESS or error code

-----------------------------------------------------------------------------*/
static int nexusIO_init(void)
{
    printk("%s: init\n", driver_name);

    if (nexusIO_hwaddr == 0)
    {
        printk(KERN_ERR "%s: No address specified for reserved memory\n", driver_name);
        return -ENODEV;
    }

    if (nexusIO_length == 0)
    {
        printk(KERN_ERR "%s: No length specified for reserved memory\n", driver_name);
        return -ENODEV;
    }

    printk("%s: nexusIO_hwaddr=0x%lx, nexusIO_length=0x%lx\n",
        driver_name, nexusIO_hwaddr, nexusIO_length);

    /* register device */
    if (misc_register(&nexusIO_dev) < 0)
    {
        printk(KERN_ERR "%s: Unable to register nexusIO misc device\n", driver_name);
        return -ENODEV;
    }
    printk("%s: Registered device '/dev/%s'\n", driver_name, NEXUS_NAME);

    printk("%s: Sucessfully installed driver\n", driver_name);
    return SUCCESS;
}

/*-----------------------------------------------------------------------------
NAME
    nexusIO_exit

DESCRIPTION
    mandatory module exit function

RETURNS
    void

-----------------------------------------------------------------------------*/
static void nexusIO_exit(void)
{
    printk("%s: exit\n", driver_name);

    misc_deregister(&nexusIO_dev);
}

module_init(nexusIO_init);
module_exit(nexusIO_exit);
