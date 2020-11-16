/*
 * linux/drivers/char/pci-demo.c
 *
 * PCI device sample code
 *
 */

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/wait.h>
#include <linux/slab.h>

#define DEVNAME		"pci-demo"
#define DEVMAJOR	224

#define BUF_SIZE	(1*1024*1024)

static int dma_enabled = 0;
static dma_addr_t *dma_buf = NULL;
static dma_addr_t dma_phys;
static dma_addr_t src_phys;

static volatile int dma_finished = 0;
static DECLARE_WAIT_QUEUE_HEAD(wq);

struct pci_demo_dev {
	unsigned long memaddr;
	void __iomem *membase;
	unsigned long memlen;
	struct pci_dev *dev;
};

static struct pci_demo_dev demo;

static int pci_demo_open(struct inode * inode, struct file * file)
{
	return 0;
}

static ssize_t pci_demo_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	printk("pci-demo: hello world %ld\n", count);
	return count;
}

static void dma_tx_callback(void *dma_async_param)
{
	dma_finished = 1;
	wake_up_interruptible(&wq);
}

static int dma_copy(size_t sz)
{
	struct dma_chan *chan = NULL;
	struct dma_device *dma_dev;
	dma_cap_mask_t mask;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_cookie_t cookie;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	chan = dma_request_channel(mask, NULL, NULL);
	if (!chan) {
		printk(KERN_ERR"pci-demo: dma channel request failed.\n");
		return -1;
	}
	dma_dev = chan->device;

	dma_finished = 0;
	tx = dma_dev->device_prep_dma_memcpy(chan, dma_phys, src_phys,
						sz, DMA_PREP_INTERRUPT|DMA_CTRL_ACK);
	if (!tx) {
		printk(KERN_ERR"pci-demo: failed to request dma tx\n");
		return -1;
	}

	tx->callback = dma_tx_callback;
	cookie = dmaengine_submit(tx);
	if (dma_submit_error(cookie)) {
		printk(KERN_ERR"pci-demo: failed to do dma tx submit\n");
	}

	dma_async_issue_pending(chan);
	wait_event_interruptible(wq, dma_finished);

	dma_release_channel(chan);
	return 0;
}

static ssize_t pci_demo_read(struct file * file, char __user * buf,
				size_t count, loff_t *ppos)
{
	if (!demo.membase)
		return -EIO;

	if (count > demo.memlen)
		count = demo.memlen;

	if (dma_enabled)
		dma_copy(count);
	else
		memcpy(dma_buf, demo.membase, count);

	if (copy_to_user(buf, dma_buf, count))
		return -EFAULT;
	return count;
}

static const struct file_operations pci_demo_fops = {
	.owner		= THIS_MODULE,
	.open		= pci_demo_open,
	.write		= pci_demo_write,
	.read		= pci_demo_read,
	//.ioctl	= pci_demo_ioctl,
	//.mmap		= pci_demo_mmap,
	//.release	= pci_demo_release,
	.llseek = no_llseek,
};

static int __init pci_demo_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	if (pci_enable_device(pci_dev))
		return -EIO;

	demo.dev = pci_dev;

	demo.memaddr = pci_resource_start(pci_dev, 0);
	demo.memlen = pci_resource_len(pci_dev, 0);
	if (!request_mem_region(demo.memaddr, demo.memlen,"pci-demo")){
		printk(KERN_ERR"pci-demo: request_mem_region failed.\n");
		return -EIO;
	}

	demo.membase = ioremap(demo.memaddr, demo.memlen);
	if (!demo.membase) {
		pci_disable_device(pci_dev);
		printk(KERN_ERR"pci-demo: ioremap failed.\n");
		return -EIO;
	}

	if (register_chrdev(DEVMAJOR, DEVNAME, &pci_demo_fops)) {
		iounmap(demo.membase);
		pci_disable_device(pci_dev);
		printk(KERN_ERR"pci-demo: cannot register char device.\n");
		return -EIO;
	}

	dma_buf = dma_alloc_coherent(NULL, BUF_SIZE, &dma_phys, GFP_KERNEL);
	if (dma_buf) {
		src_phys = dma_map_single(NULL, demo.membase, demo.memlen, DMA_FROM_DEVICE);
		if (src_phys) {
			dma_enabled = 1;
			printk(KERN_INFO"pci-demo: dma enabled, %#x -> %#x\n",
					(unsigned int)src_phys, (unsigned int)dma_phys);
		}
	}

	pci_set_master(pci_dev);
	pci_set_drvdata(pci_dev, &demo);
	printk(KERN_INFO"pci-demo: device probed!\n");
	return 0;
}

static void pci_demo_remove(struct pci_dev *pci_dev)
{
	unregister_chrdev(DEVMAJOR, DEVNAME);
	dma_free_coherent(NULL, BUF_SIZE, dma_buf, dma_phys);
	dma_unmap_single(NULL, src_phys, demo.memlen, DMA_FROM_DEVICE);
	if (demo.membase) {
		iounmap(demo.membase);
		release_mem_region(demo.memaddr, demo.memlen);
	}
	pci_disable_device(pci_dev);
	printk(KERN_INFO"pci-demo: device removed!\n");
}


#define PCI_VENDOR_ID_DEMO	0x1234
#define PCI_DEVICE_ID_DEMO	0x4567

static struct pci_device_id pci_demo_tbl [] = {
	{PCI_DEVICE(PCI_VENDOR_ID_DEMO, PCI_DEVICE_ID_DEMO)},
	{}
};

MODULE_DEVICE_TABLE(pci, pci_demo_tbl);

static struct pci_driver pci_demo_driver = {
	.name		= DEVNAME,
	.id_table	= pci_demo_tbl,
	.probe		= pci_demo_probe,
	.remove		= pci_demo_remove,
};

static int __init pci_demo_init(void)
{
	printk("#################################################\n");
	printk(KERN_INFO"pci-demo: register driver\n");
	memset(&demo, 0, sizeof(demo));
	return pci_register_driver(&pci_demo_driver);
}

static void __exit pci_demo_exit(void)
{
	pci_unregister_driver(&pci_demo_driver);
}

module_init(pci_demo_init);
module_exit(pci_demo_exit);

MODULE_AUTHOR("Li Xiaobo <lixiaobo@newbeiyang.com>");
MODULE_DESCRIPTION("PCI Demo Driver");
MODULE_LICENSE("GPL");

