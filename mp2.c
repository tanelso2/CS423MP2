#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h> 
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include "mp2_given.h"

#define DEBUG 1

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_06");
MODULE_DESCRIPTION("CS-423 MP2");

//for the /proc/mp2 directory
static struct proc_dir_entry *proc_dir;
//for the /proc/mp2/status file
static struct proc_dir_entry *proc_entry;

struct mp2_task_struct {
	struct task_struct* linux_task;
	int pid;
	int period;
	int computation;
	//might need more. Not sure yet
}

void mp2_register(void) {
	printk(KERN_INFO "register called\n");
}

void mp2_yield(void) {
	printk(KERN_INFO "yield called\n");
}

void mp2_deregister(void) {
	printk(KERN_INFO "deregister called\n");
}

static char input_buf[80];

static ssize_t mp2_write(struct file *file, const char __user *buffer, size_t count, loff_t *data) {
	if (copy_from_user(input_buf, buffer, count) ) {
		return -EFAULT;
	}

	switch (input_buf[0]) {
		case 'R':
			mp2_register();
			break;
		case 'Y':
			mp2_yield();
			break;
		case 'D':
			mp2_deregister();
			break;
		default:
			printk(KERN_ALERT "invalid write to proc/mp2/status\n");
			break;
	}

	return count;
}

static ssize_t mp2_read(struct file *file, char __user *buffer, size_t count, loff_t *data) {
	return count; //TODO change
}

//struct for the status procfile.
static const struct file_operations mp2_file = {
	.owner = THIS_MODULE,
	.read = mp2_read,
	.write = mp2_write,
};

/*
 * mp2_init - Called when module is loaded
 * Initializes the needed data structures
 */
int __init mp2_init(void)
{
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE LOADING\n");
	#endif

	proc_dir = proc_mkdir("mp2", NULL); 
	proc_entry = proc_create("status", 0666, proc_dir, &mp2_file);

	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	return 0;
}

/*
 * mp2_exit - Called when module is unloaded
 * Gets rid of the data structures
 */
void __exit mp2_exit(void)
{
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
	#endif

	remove_proc_entry("status", proc_dir);
	remove_proc_entry("mp2", NULL);

	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
