#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <linux/slab.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");

static struct dentry *dir, *output;
static struct task_struct *task;

struct packet {
	pid_t pid;
	unsigned long vaddr;
	unsigned long paddr;
};

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
	struct packet *p = (struct packet *)user_buffer;
	pid_t pid = p->pid;
	unsigned long va = p->vaddr;
	unsigned long ppn = 0, offset = 0;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	task = pid_task(find_vpid(pid), PIDTYPE_PID);

	pgd = pgd_offset(task->mm, va);
	p4d = p4d_offset(pgd, va);
	pud = pud_offset(p4d, va);
	pmd = pmd_offset(pud, va);
	pte = pte_offset_kernel(pmd, va);
	ppn = pte_pfn(*pte) << PAGE_SHIFT;
	offset = va & ~PAGE_MASK;

	p->paddr = ppn | offset;

	return 0;
}

static const struct file_operations dbfs_fops = {
    .read = read_output,
};

static int __init dbfs_module_init(void)
{
        dir = debugfs_create_dir("paddr", NULL);

        if (!dir) {
                printk("Cannot create paddr dir\n");
                return -1;
        }

        output = debugfs_create_file("output", 0444, dir, NULL, &dbfs_fops);

		printk("dbfs_paddr module initialize done\n");

        return 0;
}

static void __exit dbfs_module_exit(void)
{
        debugfs_remove_recursive(dir);

		printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
