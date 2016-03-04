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

#define TSK_SLEEPING 0
#define TSK_RUNNING 1
#define TSK_READY 2

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_06");
MODULE_DESCRIPTION("CS-423 MP2");

//for the /proc/mp2 directory
static struct proc_dir_entry *proc_dir;
//for the /proc/mp2/status file
static struct proc_dir_entry *proc_entry;

struct kmem_cache * mp2_cachep;

LIST_HEAD(task_list);
DEFINE_MUTEX(list_lock);

struct mp2_task_struct {
	struct task_struct* linux_task;
	struct list_head list;

	int pid;
	int period;
	int comp_time;
	int task_state;
	//might need more. Not sure yet
};

static char input_buf[80];

void mp2_register(void) {
	printk(KERN_INFO "register called\n");
	struct mp2_task_struct * new_task_entry;

	new_task_entry = kmem_cache_alloc(mp2_cachep, GFP_KERNEL);

	sscanf(input_buf, "R, %d, %d, %d", &new_task_entry->pid, &new_task_entry->period, &new_task_entry->comp_time);
	
	#ifdef DEBUG
	printk(KERN_INFO "pid=%d, period=%d, comp_time=%d\n", new_task_entry->pid, new_task_entry->period, new_task_entry->comp_time);
	#endif

	new_task_entry->linux_task = find_task_by_pid(new_task_entry->pid);	
	new_task_entry->task_state = TSK_SLEEPING;

	mutex_lock_interruptible(&list_lock);
	list_add(&new_task_entry->list, &task_list);
	mutex_unlock(&list_lock);
}

void mp2_yield(void) {
	printk(KERN_INFO "yield called\n");
}

void mp2_deregister(void) {
	printk(KERN_INFO "deregister called\n");
	int pid;
	sscanf(input_buf, "D, %d", &pid);

	struct list_head *pos, *q;
	struct mp2_task_struct *curr;
	
	mutex_lock_interruptible(&list_lock);
	list_for_each_safe(pos, q, &task_list) {
		curr = list_entry(pos, struct mp2_task_struct, list);
		if(curr->pid == pid) {
			list_del(pos);
			kmem_cache_free(mp2_cachep, curr);
		}
	}
	mutex_unlock(&list_lock);
}

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

static char output_buf[160];

static ssize_t mp2_read(struct file *file, char __user *buffer, size_t count, loff_t *data) {
	struct mp2_task_struct *curr;
	size_t size;
	if(*data > 0)
		return 0;
	mutex_lock_interruptible(&list_lock);
	size = 0;
	list_for_each_entry(curr, &task_list, list) {
		size += sprintf(output_buf + size, "%d: %d, %d\n", curr->pid, curr->period, curr->comp_time);
	}
	output_buf[size] = '\0';
	copy_to_user(buffer, output_buf, size+1);
	*data += size;
	mutex_unlock(&list_lock);
	return size + 1;
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
int __init mp2_init(void) {
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE LOADING\n");
	#endif

	proc_dir = proc_mkdir("mp2", NULL); 
	proc_entry = proc_create("status", 0666, proc_dir, &mp2_file);
	
	mp2_cachep = kmem_cache_create("mp2_tasks", sizeof(struct mp2_task_struct), ARCH_MIN_TASKALIGN, SLAB_PANIC, NULL);

	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	return 0;
}

/*
 * mp2_exit - Called when module is unloaded
 * Gets rid of the data structures
 */
void __exit mp2_exit(void) {
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
	#endif

	struct list_head *pos, *q;
	struct mp2_task_struct *curr;

	mutex_lock_interruptible(&list_lock);
	list_for_each_safe(pos, q, &task_list) {
		curr = list_entry(pos, struct mp2_task_struct, list);
		list_del(pos);
		kmem_cache_free(mp2_cachep, curr);
	}
	mutex_unlock(&list_lock);

	remove_proc_entry("status", proc_dir);
	remove_proc_entry("mp2", NULL);

	kmem_cache_destroy(mp2_cachep);

	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
