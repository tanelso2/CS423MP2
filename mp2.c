#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/mutex.h> 
#include <linux/sched.h>
#include <linux/timer.h>
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

static struct task_struct * dispatch_thread;

struct kmem_cache * mp2_cachep;

LIST_HEAD(task_list);
DEFINE_MUTEX(list_lock);
DEFINE_MUTEX(running_task_lock);

struct mp2_task_struct {
	struct task_struct* linux_task;
	struct list_head list;
    struct timer_list timer;

	int pid;
	int period;
	int comp_time;
	int task_state;
    unsigned long prev_period;
	//might need more. Not sure yet
};

struct mp2_task_struct * current_running_task = NULL;

int can_be_admitted(int period, int comp_time) {
    int sum;
    struct mp2_task_struct *iter;
    sum = 0;
	mutex_lock_interruptible(&list_lock);
    list_for_each_entry(iter, &task_list, list) {
        sum += (1000*iter->comp_time) / iter->period;
    }
    mutex_unlock(&list_lock);
	sum += (1000*comp_time) / period;
    return sum <= 693;
}

/**
 * Function to run in the dispatch thread
 */
int dispatch_func(void* data) {
	//TODO: Probably breaking this down more would be smart
	struct mp2_task_struct *iter, *next_in_line;	
	while (!kthread_should_stop()) {
        //set itself to sleep
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		
		printk(KERN_INFO "Going around the dispatch loop\n");

		iter = NULL;
		next_in_line = NULL;
		mutex_lock_interruptible(&list_lock);
		list_for_each_entry(iter, &task_list, list) {
			if (iter->task_state == TSK_READY && next_in_line == NULL) {
				next_in_line = iter;
			} else if (next_in_line != NULL && iter->period < next_in_line->period && iter->task_state == TSK_READY) {
				next_in_line = iter;
			}
		}
		mutex_lock_interruptible(&running_task_lock);
		if(current_running_task != NULL) {
			//stop the current running task
			if(current_running_task->task_state == TSK_RUNNING) {
				current_running_task->task_state = TSK_READY;
			} else {
				current_running_task->task_state = TSK_SLEEPING;
			}
			struct sched_param s;
			s.sched_priority = 0;
			sched_setscheduler(current_running_task->linux_task, SCHED_NORMAL, &s);
			
			printk(KERN_INFO "Task %d put to sleep\n", current_running_task->pid);
		}
		mutex_unlock(&running_task_lock);
		if (next_in_line != NULL) {
			//set next in line to run 
			next_in_line->task_state = TSK_RUNNING;
			struct sched_param sparam;
			wake_up_process(next_in_line->linux_task);
			sparam.sched_priority = 99;
			sched_setscheduler(next_in_line->linux_task, SCHED_FIFO, &sparam);
			mutex_lock_interruptible(&running_task_lock);
			current_running_task = next_in_line;
			mutex_unlock(&running_task_lock);
			printk(KERN_INFO "Task %d is now running\n", next_in_line->pid);
		}
		mutex_unlock(&list_lock);
	}
	return 0;
}

/**
 * When the timer goes off, set it to READY and wake up dispatch thread
 * INTERRUPT CONTEXT. Make sure it doesn't sleep
 */
void timer_handler(unsigned long pid) {
    struct mp2_task_struct *iter;
    list_for_each_entry(iter, &task_list, list) {
        if(iter->pid == pid) {
            iter->task_state = TSK_READY;
        }
    }
	printk(KERN_INFO "Task %d woken up in timer interrupt\n", pid);
    wake_up_process(dispatch_thread);
}

static char input_buf[80];

void mp2_register(void) {
	printk(KERN_INFO "register called\n");
    int period, pid, comp_time;
	struct mp2_task_struct * new_task_entry;

	sscanf(input_buf, "R, %d, %d, %d", &pid, &period, &comp_time);

    if(!can_be_admitted(period, comp_time) ) {
        return;
    }

	new_task_entry = kmem_cache_alloc(mp2_cachep, GFP_KERNEL);

    new_task_entry->pid = pid;
    new_task_entry->period = period;
    new_task_entry->comp_time = comp_time;
	
	#ifdef DEBUG
	printk(KERN_INFO "pid=%d, period=%d, comp_time=%d\n", new_task_entry->pid, new_task_entry->period, new_task_entry->comp_time);
	#endif

	new_task_entry->linux_task = find_task_by_pid(new_task_entry->pid);	
	new_task_entry->task_state = TSK_SLEEPING;
    new_task_entry->prev_period = jiffies;

    setup_timer(&new_task_entry->timer, timer_handler, new_task_entry->pid);

	mutex_lock_interruptible(&list_lock);
	list_add(&new_task_entry->list, &task_list);
	mutex_unlock(&list_lock);
}

struct mp2_task_struct* find_task(int pid) {
    struct mp2_task_struct *iter, *ret;
    ret = NULL;
    mutex_lock_interruptible(&list_lock);
    list_for_each_entry(iter, &task_list, list) {
        if(iter->pid == pid) {
            ret = iter;
        }
    }
    mutex_unlock(&list_lock);
    return ret;
}

void mp2_yield(void) {
	printk(KERN_INFO "yield called\n");
    int pid;
    sscanf(input_buf, "Y, %d", &pid);
    struct mp2_task_struct *curr;
    curr = find_task(pid);
    if (curr == NULL) {
        return;
    }
    unsigned long next_period = curr->prev_period + msecs_to_jiffies(curr->period);
    if(next_period <= jiffies) {
        /* Do Nothing because the next period has already started */
		printk(KERN_ALERT "Failed to meet period for task %d\n", pid);
    } else {
        curr->task_state = TSK_SLEEPING;
        set_task_state(curr->linux_task, TASK_UNINTERRUPTIBLE);
        mod_timer(&curr->timer, next_period);
		if (current_running_task == curr) {
			mutex_lock_interruptible(&running_task_lock);
			current_running_task = NULL;
			mutex_unlock(&running_task_lock);
		}
    }
	curr->prev_period = next_period;

    wake_up_process(dispatch_thread);
	printk(KERN_INFO "%d yielded\n", pid);

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
			mutex_lock_interruptible(&running_task_lock);
            if(curr == current_running_task) {
                current_running_task = NULL;
            }
			mutex_unlock(&running_task_lock);
            del_timer(&curr->timer);
			list_del(pos);
			kmem_cache_free(mp2_cachep, curr);
			printk(KERN_INFO "Should have deregistered task %d\n", pid);
		}
	}
	list_for_each_entry(curr, &task_list, list) {
		printk(KERN_INFO "Going through the task list after degistering %d and I found %d\n", pid, curr->pid);
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
	
	dispatch_thread = kthread_run(dispatch_func, NULL, "mp2 dispatch thread");

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

	kthread_stop(dispatch_thread);

	mutex_lock_interruptible(&list_lock);
	list_for_each_safe(pos, q, &task_list) {
		curr = list_entry(pos, struct mp2_task_struct, list);
        del_timer(&curr->timer);
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
