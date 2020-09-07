#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#define MAX_LENGTH 1000

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;

typedef struct debugfs_blob_wrapper bw;
bw *my_bw;
char *answer;

static ssize_t write_pid_to_input(struct file *fp, 
                                const char __user *user_buffer, 
                                size_t length, 
                                loff_t *position)
{
        pid_t input_pid;
		char tmp[MAX_LENGTH];
		int i;

		for(i = 0; i < MAX_LENGTH; i++) answer[i] = 0;
		
		sscanf(user_buffer, "%u", &input_pid);
        curr = pid_task(find_vpid(input_pid), PIDTYPE_PID);

		if(curr == NULL) {
			printk("Invalid PID\n");
			return length;
		}

		while(curr) {
			sprintf(tmp, "%s (%d)\n%s", curr->comm, curr->pid, answer);
			strcpy(answer, tmp);
			if(curr->pid == 1) break;
			curr = curr->real_parent;	
		}

        return length;
}

static const struct file_operations dbfs_fops = {
        .write = write_pid_to_input,
};

static int __init dbfs_module_init(void)
{
        dir = debugfs_create_dir("ptree", NULL);
        
        if (!dir) {
                printk("Cannot create ptree dir\n");
                return -1;
        }

		answer = (char *)kmalloc(MAX_LENGTH * sizeof(char), GFP_KERNEL);
		my_bw = (bw *)kmalloc(sizeof(bw), GFP_KERNEL);
		my_bw->data = (void *)answer;
		my_bw->size = (unsigned long)MAX_LENGTH * sizeof(char);

        inputdir = debugfs_create_file("input", 0644, dir, NULL, &dbfs_fops);
        ptreedir = debugfs_create_blob("ptree", 0444, dir, my_bw);
	
		printk("dbfs_ptree module initialize done\n");

        return 0;
}

static void __exit dbfs_module_exit(void)
{
    debugfs_remove_recursive(dir);
	kfree(my_bw);
	kfree(answer);
	
	printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
