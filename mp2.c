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
#include <linux/string.h>
#include <linux/spinlock.h>
#include "mp2_given.h"

#define DEBUG 1

#define TSK_SLEEPING 0
#define TSK_RUNNING 1
#define TSK_READY 2

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_06");
MODULE_DESCRIPTION("CS-423 MP2");

// Proc file
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

// Dispatch thread
static struct task_struct * dispatch_thread;

// Cache slab
struct kmem_cache * mp2_cachep;

// IO buffers
static char input_buf[80];
static char output_buf[160];

// Task list
LIST_HEAD(task_list);

// Locks
DEFINE_MUTEX(list_lock);
DEFINE_MUTEX(running_task_lock);
DEFINE_SPINLOCK(spinning_lock);

/*
 * Struct holding task variables
*/
struct mp2_task_struct {
	struct task_struct* linux_task;
	struct list_head list;
    struct timer_list timer;
	int pid;
	int period;
	int comp_time;
	int task_state;
    unsigned long prev_period;
};

/*
 * Reference to current running task
*/
struct mp2_task_struct * current_running_task = NULL;

/**
 * Function that returns true or false for whether the
 * task with the given period and comp_time can be admitted
 */
int can_be_admitted(int period, int comp_time) {
    int sum = 0;
    struct mp2_task_struct *iter;
	
	// Lock list to fetch stat for each task
	mutex_lock_interruptible(&list_lock);
    list_for_each_entry(iter, &task_list, list) {
        sum += (1000*iter->comp_time) / iter->period;
    }
    mutex_unlock(&list_lock);
	
	// Return whether admitting task is valid
	sum += (1000*comp_time) / period;
    return sum <= 693;
}

/**
 * Function to run in the dispatch thread
 */
int dispatch_func(void* data) {
	struct mp2_task_struct *iter, *next_in_line;	
	
	// Loop should break when kthread_stop() is called from exit
	while (!kthread_should_stop()) {
        
		//set itself to sleep
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		
		iter = NULL;
		next_in_line = NULL;
		
		// Lock the list
		mutex_lock_interruptible(&list_lock);

		list_for_each_entry(iter, &task_list, list) {

			// Retrieve from list the next ready task
			if (iter->task_state == TSK_READY && next_in_line == NULL) {
				next_in_line = iter;
			} else if (next_in_line != NULL && iter->period < next_in_line->period && iter->task_state == TSK_READY) {
				next_in_line = iter;
			}

		}
		
		// Lock the running task
		mutex_lock_interruptible(&running_task_lock);
		if(current_running_task != NULL) {

			// Set state of current task for change			
			if(current_running_task->task_state == TSK_RUNNING) {
				current_running_task->task_state = TSK_READY;
			} else {
				current_running_task->task_state = TSK_SLEEPING;
			}

			// Schedule current task with new state
			struct sched_param s;
			s.sched_priority = 0;
			sched_setscheduler(current_running_task->linux_task, SCHED_NORMAL, &s);
			
		}
		mutex_unlock(&running_task_lock); // Unlock running task
		
		if (next_in_line != NULL) {
 
			// Wake up next task	
			next_in_line->task_state = TSK_RUNNING;
			wake_up_process(next_in_line->linux_task);
			struct sched_param sparam;
			sparam.sched_priority = 90;
			sched_setscheduler(next_in_line->linux_task, SCHED_FIFO, &sparam);
			
			// Set reference to current task
			mutex_lock_interruptible(&running_task_lock);
			current_running_task = next_in_line;
			mutex_unlock(&running_task_lock);
		}

		mutex_unlock(&list_lock); // Unlock the list
	}
	return 0;
}

/*
 * Timer handler, sets new task to ready and wakes dispatch loop
 * Runs in interrupt context
 */
void timer_handler(unsigned long pid) {
    struct mp2_task_struct *iter;
	unsigned long flags;
	spin_lock_irqsave(&spinning_lock, flags);
    list_for_each_entry(iter, &task_list, list) {
        if(iter->pid == pid) {
            iter->task_state = TSK_READY;
        }
    }
    wake_up_process(dispatch_thread);
	spin_unlock_irqrestore(&spinning_lock, flags);
}

/*
 * Registers task to list from global input buffer
 */
void mp2_register(void) {
    int period, pid, comp_time;
	struct mp2_task_struct * new_task_entry;

	// Read new task from input buffer
	sscanf(input_buf, "R, %d, %d, %d", &pid, &period, &comp_time);

	// Break if task cannot be admitted
    if(!can_be_admitted(period, comp_time) ) {
		printk(KERN_ALERT "Task with pid %d can not be admitted\n", pid);
        return;
    }

	// Create a new task
	new_task_entry = kmem_cache_alloc(mp2_cachep, GFP_KERNEL);
    new_task_entry->pid = pid;
    new_task_entry->period = period;
    new_task_entry->comp_time = comp_time;
	new_task_entry->linux_task = find_task_by_pid(new_task_entry->pid);	
	new_task_entry->task_state = TSK_SLEEPING;
    new_task_entry->prev_period = jiffies;
    setup_timer(&new_task_entry->timer, timer_handler, new_task_entry->pid);

	// Lock the list to add entry to task list
	mutex_lock_interruptible(&list_lock);
	list_add(&new_task_entry->list, &task_list);
	mutex_unlock(&list_lock);
}

/*
 * Helper function, finds task from list by pid
 */
struct mp2_task_struct* find_task(int pid) {
    struct mp2_task_struct *iter, *ret;
    ret = NULL;
    
	// Lock list to traverse
	mutex_lock_interruptible(&list_lock);
    list_for_each_entry(iter, &task_list, list) {
        if(iter->pid == pid) {
            ret = iter;
        }
    }
    mutex_unlock(&list_lock);
    
	return ret;
}

/*
 * Yields task given by global input buffer
 */
void mp2_yield(void) {
    struct mp2_task_struct *curr;
    int pid;
    sscanf(input_buf, "Y, %d", &pid);

	// Get current task from given pid
    curr = find_task(pid);

	// If task to yield not register, return error
    if (curr == NULL) {
		printk(KERN_ALERT "Currently yielding task %d not found\n", pid);
        return;
    }

	// Calculate next period
    unsigned long next_period; 
	next_period = curr->prev_period + msecs_to_jiffies(curr->period);
	if(next_period <= jiffies) {
		// If next period has started, pass
		printk(KERN_ALERT "Failed to meet period for task %d\n", pid);
    } else {
		// Otherwise set current task to sleep
        curr->task_state = TSK_SLEEPING;
        set_task_state(curr->linux_task, TASK_UNINTERRUPTIBLE);
        mod_timer(&curr->timer, next_period);
		if (current_running_task == curr) {
			mutex_lock_interruptible(&running_task_lock);
			current_running_task = NULL;
			mutex_unlock(&running_task_lock);
		}
    }

	// Set next period for task
	curr->prev_period = next_period;

	// Wake up dispatch thread for scheduling
    wake_up_process(dispatch_thread);
}

/*
 * Deregisters a task given by global input buffer
 */
void mp2_deregister(void) {
	int pid;
	sscanf(input_buf, "D, %d", &pid);

	struct list_head *pos, *q;
	struct mp2_task_struct *curr;
	
	// Lock list, traverse 'safe' because we remove entry
	mutex_lock_interruptible(&list_lock);
	list_for_each_safe(pos, q, &task_list) {

		curr = list_entry(pos, struct mp2_task_struct, list);
		if(curr->pid == pid) {
			
			// If task to deregister is current, lock the current task before setting to NULL
			mutex_lock_interruptible(&running_task_lock);
            if(curr == current_running_task) {
                current_running_task = NULL;
            }
			mutex_unlock(&running_task_lock);
            
			// Clean up task to deregister
			del_timer(&curr->timer);
			list_del(pos);
			kmem_cache_free(mp2_cachep, curr);
		}

	}
	mutex_unlock(&list_lock); // Unlock list
}

/*
 * Write function reads to input buffer and passes to helper function
 */
static ssize_t mp2_write(struct file *file, const char __user *buffer, size_t count, loff_t *data) {
	// Read to input buffer
	if (copy_from_user(input_buf, buffer, count) ) {
		return -EFAULT;
	}

	// Call appropriate action function
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

	// Clear input buffer to prevent trailing residual input
	memset(input_buf, 0, 80);
	return count;
}

/*
 * Read function to user app, prints contents of task list to output buffer
 */
static ssize_t mp2_read(struct file *file, char __user *buffer, size_t count, loff_t *data) {
	struct mp2_task_struct *curr;
	size_t size;
	
	// If requesting extra data, return 0 to stop
	if(*data > 0)
		return 0;

	// Lock list
	mutex_lock_interruptible(&list_lock);
	size = 0;
	list_for_each_entry(curr, &task_list, list) {
		// Append entry as string to ouput buffer, add length of string to size
		size += sprintf(output_buf + size, "%d: %d, %d\n", curr->pid, curr->period, curr->comp_time);
	}
	mutex_unlock(&list_lock); // Unlock list
	
	// Set last character to end of string, return size of buffer
	output_buf[size] = '\0';
	copy_to_user(buffer, output_buf, size+1);
	*data += size;
	return size + 1;
}

/*
 * Static procfile struct
 */
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

	// Create procfile
	proc_dir = proc_mkdir("mp2", NULL); 
	proc_entry = proc_create("status", 0666, proc_dir, &mp2_file);
	
	// Init cache slab for task entries
	mp2_cachep = kmem_cache_create("mp2_tasks", sizeof(struct mp2_task_struct), ARCH_MIN_TASKALIGN, SLAB_PANIC, NULL);

	// Create dispatch thread
	struct sched_param sparam;	
	dispatch_thread = kthread_run(dispatch_func, NULL, "mp2 dispatch thread");
	sparam.sched_priority = 99;
	sched_setscheduler(dispatch_thread, SCHED_FIFO, &sparam);

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

	// Signals dispatch thread to return
	kthread_stop(dispatch_thread);

	// Delete and clean up entries from list
	mutex_lock_interruptible(&list_lock);
	list_for_each_safe(pos, q, &task_list) {
		curr = list_entry(pos, struct mp2_task_struct, list);
        del_timer(&curr->timer);
		list_del(pos);
		kmem_cache_free(mp2_cachep, curr);
	}
	mutex_unlock(&list_lock);

	// Remove proc file
	remove_proc_entry("status", proc_dir);
	remove_proc_entry("mp2", NULL);

	// Clean cache
	kmem_cache_destroy(mp2_cachep);

	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
