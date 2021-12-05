/*
 *  fpga_drv.c
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <linux/dma-mapping.h>

#ifndef CONFIG_OF  // If no device-tree support in kernel, turn it off here
#define NO_DTS
#endif

#define fpga_VERSION "1.0"
#define fpga_NAME    "fpga_drv"

/* Without device-tree support, compile in hardcoded resource info */
#ifdef NO_DTS
#define INTERRUPT 54   // This is the linux-mapped irq, not the HW/GIC irq.
                       // Can be found by (GIC IRQ is 121 for this example):
                       //   grep 121 /sys/kernel/irq/*/hwirq

#define FPGA_BASE    0x2000000000ULL
#endif

#define FPGA_MASK    0x00ffffff
#define FPGA_SIZE    0x01000000

#define COMMAND_MASK 0x80000000

MODULE_AUTHOR("gerstl@ece.utexas.edu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FPGA Device Driver");

#define DRIVER_NAME "fpga"

#ifdef NO_DTS
/* command line parameters */
unsigned install = 0;
module_param(install, int, S_IRUGO);
#endif

/* count of received interrupts */
int interruptcount = 0;

static struct platform_device *fpga_dev = NULL;
static struct device* dev = NULL;

#define FPGA_ABSIZE (2 * 1024 * 1024 * sizeof(short))
#define FPGA_ABORDER (23)
#define FPGA_CSIZE  (1 * 1024 * 1024 * sizeof(int))
#define FPGA_CORDER (22)
bool isa = true;
unsigned long abuf = 0;
unsigned long bbuf = 0;
unsigned long cbuf = 0;
dma_addr_t dmaabuf;
dma_addr_t dmabbuf;
dma_addr_t dmacbuf;

/* instance-specific driver-internal data structure */
static struct fpga_drv_local {
  int irq;
  unsigned long mem_start;
  unsigned long mem_end;
  volatile unsigned char *fpga_ptr;
  unsigned int offset;
  struct proc_dir_entry *fpga_interrupt_file;
  struct fasync_struct *fasync_fpga_queue ;
} l;

DECLARE_WAIT_QUEUE_HEAD(fpga_wait);


/* =========================================================================
 * Interrupt handling
 */

/* Interrupt handler */
static irqreturn_t fpga_int_handler(int irq, void *lp)
{
   interruptcount++;

#ifdef DEBUG
   printk(KERN_ALERT "\nfpga_drv: Interrupt detected in kernel \n");
#endif

   /* acknowledge/reset the interrupt */
   writel(0ul, (volatile unsigned int *)&l.fpga_ptr[3]);

   /* Signal the user application that an interupt occured */
   kill_fasync(&((struct fpga_drv_local*)lp)->fasync_fpga_queue, SIGIO, POLL_IN);

   return IRQ_HANDLED;
}


/* =========================================================================
 * Driver access methods
 */

/* Driver access routines */
static int fpga_open1 (struct inode *inode, struct file *file) {
   //abuf = __get_free_pages(GFP_KERNEL, FPGA_ABORDER);
   //bbuf = __get_free_pages(GFP_KERNEL, FPGA_ABORDER); // lol error handlign
   //cbuf = __get_free_pages(GFP_KERNEL, FPGA_CORDER);
//   struct device *dev = &fpga_dev->dev; 

   abuf = kmalloc(FPGA_ABSIZE,GFP_DMA);  
   bbuf = kmalloc(FPGA_ABSIZE,GFP_DMA);
   cbuf = kmalloc(FPGA_CSIZE,GFP_DMA);
	printk(KERN_ALERT "allocated\n");
   
   dmaabuf = dma_map_single(dev, abuf, FPGA_ABSIZE, DMA_TO_DEVICE);
   if(dma_mapping_error(dev,dmaabuf))
	printk(KERN_ALERT "dmaabuf broke\n");
   dmabbuf = dma_map_single(dev, bbuf, FPGA_ABSIZE, DMA_TO_DEVICE);
   
   dmacbuf = dma_map_single(dev, cbuf, FPGA_CSIZE, DMA_FROM_DEVICE);
   
   #ifdef DEBUG
	printk(KERN_ALERT " dma abuf %lx bbuf %lx cbuf %lx\n", dmaabuf, dmabbuf, dmacbuf);
	printk(KERN_ALERT " abuf %lx bbuf %lx cbuf %lx fpga_ptr %lx\n", abuf, bbuf, cbuf, l.fpga_ptr); 
   #endif

   writel(dmaabuf,l.fpga_ptr+16+0);
   writel(dmabbuf,l.fpga_ptr+16+8);
   writel(dmacbuf,l.fpga_ptr+16+16);
	printk(KERN_ALERT "set addresses\n");
   return 0;
}

static int fpga_release1 (struct inode *inode, struct file *file) {
   dma_unmap_single(dev, dmaabuf, FPGA_ABSIZE, DMA_TO_DEVICE);
   dma_unmap_single(dev, dmabbuf, FPGA_ABSIZE, DMA_TO_DEVICE);
   dma_unmap_single(dev, dmacbuf, FPGA_CSIZE, DMA_FROM_DEVICE);
   kfree(abuf);
   kfree(bbuf);
   kfree(cbuf);
   return 0;
}

static int fpga_fasync1 (int fd, struct file *filp, int on)
{
#ifdef DEBUG
   printk(KERN_ALERT "\nfpga_drv: Inside fpga_fasync \n");
#endif
   return fasync_helper(fd, filp, on, &l.fasync_fpga_queue);

} 

static ssize_t fpga_write1(struct file *filp, const char __user *buf, size_t count, loff_t *offp)
{
    int not_copied;
 //   struct device *dev = &fpga_dev->dev;

#ifdef DEBUG
    printk(KERN_ALERT "\nfpga_drv: receive write command to fpga \n");
#endif    
   if (isa) {
     dma_sync_single_for_cpu(dev, dmaabuf, FPGA_ABSIZE, DMA_TO_DEVICE);
     not_copied = copy_from_user((void *)abuf, buf, count);
     dma_sync_single_for_device(dev, dmaabuf, FPGA_ABSIZE, DMA_TO_DEVICE);
   } else {
     dma_sync_single_for_cpu(dev, dmabbuf, FPGA_ABSIZE, DMA_TO_DEVICE);
     not_copied = copy_from_user((void *)bbuf, buf, count);
     dma_sync_single_for_device(dev, dmabbuf, FPGA_ABSIZE, DMA_TO_DEVICE);
   }
   isa = !isa;

    //not_copied = copy_from_user((void *)l.fpga_ptr, buf, count);

    return count - not_copied;

}

static ssize_t fpga_read1(struct file *filp, char __user *buf, size_t count, loff_t *offp)
{
    int not_copied;
//    struct device *dev = &fpga_dev->dev;

#ifdef DEBUG
    printk(KERN_ALERT "\nfpga_drv: receive read command from fpga \n");
#endif    
    dma_sync_single_for_cpu(dev, dmacbuf, FPGA_CSIZE, DMA_FROM_DEVICE);
    not_copied = copy_to_user(buf, (void *)cbuf, count);
    dma_sync_single_for_device(dev, dmacbuf, FPGA_CSIZE, DMA_FROM_DEVICE);
    //not_copied  = copy_to_user(buf, (void *)l.fpga_ptr, count);

    return count - not_copied;
}

static long fpga_ioctl1(struct file *file, unsigned int cmd, unsigned long arg) {

   int retval = 0;
   unsigned long value;
   unsigned int command_type;
   unsigned int offset;
   volatile unsigned char *access_addr;

#ifdef DEBUG
   printk(KERN_ALERT "\nfpga_drv: Inside fpga_ioctl1 \n");
#endif
   if (cmd >= FPGA_SIZE && cmd <= FPGA_SIZE + 2) {
      if (cmd == FPGA_SIZE) {
         return dmaabuf;
      }
      if (cmd == FPGA_SIZE + 1) {
         return dmabbuf;
      }
      if (cmd == FPGA_SIZE + 2) {
         return dmacbuf;
      }
   }

   // Set the offset for register accesses
   offset = ~COMMAND_MASK & cmd & FPGA_MASK;
   if(offset > FPGA_SIZE)
      retval=-EINVAL;

   command_type = COMMAND_MASK & cmd;
   switch(command_type)
   {
      case 0:
         //read
         if(!access_ok(VERIFY_READ, (unsigned int *)arg, sizeof(int)))
            return -EFAULT;

	 value = readl((volatile unsigned int *)&l.fpga_ptr[offset]);
	 put_user(value, (unsigned long*)arg);

#ifdef DEBUG
         printk("fpga_drv: Read value %08lx\n", value);
#endif
         break;

      case COMMAND_MASK:
         //write
         access_addr = l.fpga_ptr + offset;

         if(!access_ok(VERIFY_WRITE, (unsigned int *)arg, sizeof(int)))
            return -EFAULT;

         get_user(value, (unsigned long *)arg);
         writel(value, access_addr); 

#ifdef DEBUG
         printk("fpga_drv: Wrote value %08lx\n", value);
#endif
         break;

      default:
#ifdef DEBUG
         printk(KERN_ERR "fpga_drv: Invalid command \n");
#endif
         retval = -EINVAL;
   }

   return retval;
}

/* define which file operations are supported by the driver */
struct file_operations fpga_fops = {
   .owner=   THIS_MODULE,
   .llseek  = NULL,
   .read    = fpga_read1,
   .write   = fpga_write1,
   .iterate = NULL,
   .poll    = NULL,
   .compat_ioctl = NULL,
   .unlocked_ioctl = fpga_ioctl1,
   .mmap    = NULL,
   .open    = fpga_open1,
   .flush   = NULL,
   .release = fpga_release1,
   .fsync   = NULL,
   .fasync  = fpga_fasync1,
   .lock    = NULL,
   .sendpage = NULL,
   .get_unmapped_area = NULL,
   .check_flags = NULL,
   .flock = NULL,
   .splice_write = NULL,
   .splice_read = NULL,
   .setlease = NULL,
   .fallocate = NULL,
   .show_fdinfo = NULL
};


/* =========================================================================
 * /proc entry
 */

/* Operations for /proc filesystem accesses */
static int proc_read_fpga_interrupt(struct seq_file *f, void *v)
{
  seq_printf(f, "Total number of interrupts %19i\n", interruptcount);

  return 0;
}

static int proc_open_fpga_interrupt(struct inode *inode, struct  file *file) {
  return single_open(file, proc_read_fpga_interrupt, NULL);
}

/* operations supported on the /proc entry */
static const struct file_operations proc_fops = {
  .owner = THIS_MODULE,
  .open = proc_open_fpga_interrupt,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};


/* =========================================================================
 * Device handling
 */

/* /dev entry */
static struct miscdevice fpga_miscdev = {
        .minor =        MISC_DYNAMIC_MINOR,
        .name =         DRIVER_NAME,
        .fops =         &fpga_fops,
};


/* probbe and install module instance for device */
static int fpga_drv_probe (struct platform_device *pdev) 
{
  struct resource *r_irq; /* Interrupt resources */
  struct resource *r_mem; /* IO mem resources */
  //struct device *dev = &pdev->dev;
  dev = &pdev->dev;


   int rv = -EBUSY;

   dev_info(dev, "FPGA Device Tree Probing\n");

   // register device with the kernel
   if (misc_register(&fpga_miscdev)) 
   {
      dev_err(dev, "fpga_drv: unable to register device. ABORTING!\n");
      return -EBUSY;
   }

   // get memory region assigned to the device
   r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
   if (!r_mem) {
     dev_err(dev, "fpga_drv: invalid address\n");
     rv = -ENODEV;
     goto no_mem;
   }

   l.mem_start = r_mem->start;
   l.mem_end = r_mem->end;

   // perform memory REMAP
   if(!request_mem_region(l.mem_start, l.mem_end - l.mem_start + 1, fpga_NAME))
   {
      dev_err(dev, "fpga_drv: Unable to acquire FPGA address.\n");
      goto no_mem;
   }

   l.fpga_ptr = (volatile unsigned char *)ioremap(l.mem_start, 
                                                 l.mem_end - l.mem_start + 1);
   if (!l.fpga_ptr)
   {
      dev_err(dev, "fpga_drv: Unable to map FPGA.\n");
      goto no_mem;
   }

   dev_info(dev, "fpga_drv: 0x%08lx size 0x%08lx mapped to 0x%08lx\n", 
            l.mem_start, l.mem_end - l.mem_start + 1, 
            (unsigned long)l.fpga_ptr);
   dev_info(dev, "fpga_drv: using (major, minor) number (10, %d) on %s\n", 
            fpga_miscdev.minor, DRIVER_NAME); 
/*
   // create /proc file system entry
   l.fpga_interrupt_file = proc_create(DRIVER_NAME, 0444, NULL, &proc_fops);
   if(l.fpga_interrupt_file == NULL)
   {
      dev_err(dev, "fpga_drv: create /proc entry returned NULL. ABORTING!\n");
      rv = -ENOMEM;
      goto no_proc;
   }

   // get the interrupt assigned to the device
   r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
   if (!r_irq) {
     dev_info(dev, "fpga_drv: no IRQ found\n");
     goto no_fpga_interrupt;
   } 

   l.irq = r_irq->start;

   // request interrupt from linux 
   rv = request_irq(l.irq, &fpga_int_handler, 0, DRIVER_NAME,  &l);
   if (rv)
   {
      dev_err(dev, "fpga_drv: Can't get interrupt %d: %d\n", l.irq, rv);
      goto no_fpga_interrupt;
   }

   dev_info(dev, "fpga_drv: using interrupt %d\n", l.irq);
*/
   // everything initialized
   dev_info(dev, "fpga_drv: %s %s Initialized\n", fpga_NAME, fpga_VERSION);
   return 0;

   // error handling
//no_fpga_interrupt:
//   remove_proc_entry(DRIVER_NAME, NULL);
no_proc:
   release_mem_region(l.mem_start, l.mem_end - l.mem_start + 1);
no_mem:
   misc_deregister(&fpga_miscdev);
   return rv;
}

/* remove driver from kernel */
static int fpga_drv_remove (struct platform_device *pdev) 
{
   struct device *dev = &pdev->dev;

   // free interrupt
   free_irq(l.irq, &l);

   // unmap memory
   iounmap((void *)l.fpga_ptr);
   release_mem_region(l.mem_start, l.mem_end - l.mem_start + 1);
   dev_info(dev, "fpga_drv: Device released.\n");

   // de-register driver with kernel
   misc_deregister(&fpga_miscdev);

   // remove /proc entry
//   remove_proc_entry(DRIVER_NAME, NULL);

   dev_info(dev, "fpga_drv: %s %s removed\n", fpga_NAME, fpga_VERSION);

   return 0;
}

#ifdef CONFIG_OF
static struct of_device_id fpga_drv_of_match[] = {
        { .compatible = "xlnx,top-1.0", },
        { /* end of list */ },
};
MODULE_DEVICE_TABLE(of, fpga_drv_of_match);
#else
# define fpga_drv_of_match
#endif



/* Kernel driver data structure */
static struct platform_driver fpga_driver = {
  .driver = {
    .name = DRIVER_NAME,
    .owner = THIS_MODULE,
    .of_match_table = fpga_drv_of_match,
  },
  .probe          = fpga_drv_probe,
  .remove         = fpga_drv_remove,
};

#ifdef NO_DTS
/* Resources assigned to device */ 
static const struct resource fpga_resources[] = {
  {
    .start= FPGA_BASE,
    .end=   FPGA_BASE+FPGA_SIZE-1,
    .flags= IORESOURCE_MEM,
    .name= "io-memory"
    },
  {
    .start= INTERRUPT,
    .end= INTERRUPT,
    .flags= IORESOURCE_IRQ,
    .name= "irq",
    }
};
#endif

/* =========================================================================
 * Module handling
 */

/* Load and initialize module */
static int __init fpga_init_module(void)
{
   int rv = 0;

#ifdef DEBUG
   printk("FPGA Interface Module\n");
   printk(KERN_ALERT "\nfpga_drv: FPGA Driver Loading.\n");
#endif

   // register driver with kernel
   rv = platform_driver_register(&fpga_driver);
   if (rv) return rv;

#ifdef NO_DTS
   g
   // if we are asked to install the device, register (and hence probe) it
   if(install) {
     fpga_dev = platform_device_register_simple(DRIVER_NAME, -1, 
                                                &(fpga_resources[0]), 2);
     if (IS_ERR(fpga_dev)) {
       rv = PTR_ERR(fpga_dev);
       platform_driver_unregister(&fpga_driver);
       return rv;
     }
   }
#endif   
#ifdef DEBUG
      printk(KERN_ALERT "\nfpga_drv: FINISHED.\n");
#endif 
   return 0;
}

/* Unload module */
static void __exit fpga_cleanup_module(void)
{
  if(fpga_dev) platform_device_unregister(fpga_dev);
  platform_driver_unregister(&fpga_driver);
#ifdef DEBUG
  printk(KERN_ALERT "fpga_drv: Unloading.\n");
#endif
}

module_init(fpga_init_module);
module_exit(fpga_cleanup_module);

