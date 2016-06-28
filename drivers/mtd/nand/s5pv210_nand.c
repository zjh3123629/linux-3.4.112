#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#include <plat/regs-nand.h>
#include <plat/nand.h>

#define to_nand_info(pdev)		((pdev)->dev.platform_data)

struct s5pv210_nand_info {
	struct mtd_info mtd;
	struct nand_chip chip;

	struct resource *res;
	void __iomem *regs;
};
struct s5pv210_nand_info *info = NULL;

static void s5pv210_nand_hwcontrol(struct mtd_info *mtd, int cmd,
				   unsigned int ctrl)
{
	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, info->regs + S3C2440_NFCMD);
	else
		writeb(cmd, info->regs + S3C2440_NFADDR);
}

static void s5pv210_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	readsl(info->regs + S3C2440_NFDATA, buf, len >> 2);

	/* cleanup if we've got less than a word to do */
	if (len & 3) {
		buf += len & ~3;

		for (; len & 3; len--)
			*buf++ = readb(info->regs + S3C2440_NFDATA);
	}
}

static void s5pv210_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	writesl(info->regs + S3C2440_NFDATA, buf, len >> 2);

	/* cleanup any fractional write */
	if (len & 3) {
		buf += len & ~3;

		for (; len & 3; len--, buf++)
			writeb(*buf, info->regs + S3C2440_NFDATA);
	}
}

/*
 * -1:   deselect
 * else: select
 */
static void s5pv210_nand_select_chip(struct mtd_info *mtd, int chip)
{
	unsigned long cur;

	cur = readl(info->regs+S3C2440_NFCONT);

	if (chip == -1)
		cur |= S3C2440_NFCONT_nFCE;
	else
		cur &= ~S3C2440_NFCONT_nFCE;

	writel(cur, info->regs+S3C2440_NFCONT);
}

static int s5pv210_nand_devready(struct mtd_info *mtd)
{
	return readb(info->regs + S3C2440_NFESTAT1) & S3C2440_NFSTAT_READY;
}

static int s5pv210_nand_inithw(struct platform_device *pdev)
{
	info->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = ioremap(info->res->start, resource_size(info->res));
	if (NULL == info->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	/* enable the controller and de-assert nFCE */
	writel(S3C2440_NFCONT_ENABLE, info->regs + S3C2440_NFCONT);
	
	return 0;
}

static void s5pv210_mtd_chip_init(void)
{
	// fill chip & mtd_info
	info->chip.IO_ADDR_R 		= info->regs + S3C2440_NFDATA;
	info->chip.IO_ADDR_W		= info->regs + S3C2440_NFDATA;
	info->chip.cmd_ctrl 		= s5pv210_nand_hwcontrol;
	info->chip.dev_ready 		= s5pv210_nand_devready;
	info->chip.ecc.mode 		= NAND_ECC_SOFT;
	info->chip.chip_delay 		= 20;
	info->chip.read_buf 		= s5pv210_nand_read_buf;
	info->chip.write_buf 		= s5pv210_nand_write_buf;
	info->chip.select_chip		= s5pv210_nand_select_chip;
	
	// link mtd_info & nand_chip
	info->mtd.priv = &(info->chip);
	info->mtd.owner = THIS_MODULE;
}

/*
 * 1: nand_scan
 * 2: mtd_device_parse_register
 */
static int s5pv210_nand_probe(struct platform_device *pdev)
{
	struct s3c2410_platform_nand *plat = to_nand_info(pdev);
	struct s3c2410_nand_set *sets = plat->sets;

	if (NULL == sets) {
		dev_err(&pdev->dev, "can not find platform_data, check mach-xxx.c\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL) {
		dev_err(&pdev->dev, "no memory for flash info\n");
		return -ENOMEM;
	}

	s5pv210_nand_inithw(pdev);

	s5pv210_mtd_chip_init();

	// register
	/* part 1 */
	nand_scan(&info->mtd, 1);
	/* part 2 */
	mtd_device_parse_register(&info->mtd, NULL, NULL,
			sets->partitions, sets->nr_partitions);

	return 0;
}

static int s5pv210_nand_remove(struct platform_device *pdev)
{
	nand_release(&info->mtd);
	iounmap(info->regs);
	kfree(info);

	return 0;
}

static struct platform_driver s5pv210_nand_driver = {
	.probe		= s5pv210_nand_probe,
	.remove		= s5pv210_nand_remove,
	.driver		= {
		.name	= "s3c2410-nand",
		.owner	= THIS_MODULE,
	},
};

static int __init s5pv210_nand_init(void)
{
	printk("S5PV210 NAND Driver\n");

	return platform_driver_register(&s5pv210_nand_driver);
}

static void __exit s5pv210_nand_exit(void)
{
	platform_driver_unregister(&s5pv210_nand_driver);
}

module_init(s5pv210_nand_init);
module_exit(s5pv210_nand_exit);

MODULE_LICENSE("GPL");
