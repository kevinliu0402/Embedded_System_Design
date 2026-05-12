/*
 * game.c - Linux device driver for the Maze Chase Game peripheral.
 *
 * Modelled directly on lab3/lab3-sw/vga_ball.c.  We register a misc
 * character device named "game" and accept ioctls that read or write
 * the 32-bit FPGA registers mapped at the Lightweight HPS-to-FPGA
 * bridge address 0xFF200000.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include "game.h"

#define DRIVER_NAME "game"

/* Byte offsets of each 32-bit register inside the BAR. */
#define REG_EVADER_POS   0x00
#define REG_GHOST0_POS   0x04
#define REG_GHOST1_POS   0x08
#define REG_TILE_STATUS  0x0C
#define REG_TILE0_TIME   0x10
#define REG_TILE1_TIME   0x14
#define REG_TILE2_TIME   0x18
#define REG_TILE3_TIME   0x1C
#define REG_TILE4_TIME   0x20
#define REG_TILE5_TIME   0x24
#define REG_GAME_STATUS  0x28
#define REG_FRAME_SYNC   0x2C

#define ENCODE_POS(col, row) ((((uint32_t)(row) & 0x1F) << 5) | \
                              ((uint32_t)(col) & 0x1F))

/*
 * Information about our device
 */
struct game_dev {
    struct resource res;
    void __iomem  *virtbase;
    game_write_t   last_write;
} dev;

/* ------------------------------------------------------------------ */
/* Low-level register helpers                                          */
/* ------------------------------------------------------------------ */
static inline void game_write_pos(uint32_t off, game_pos_t p)
{
    iowrite32(ENCODE_POS(p.col, p.row), dev.virtbase + off);
}

static long do_write_state(const game_write_t *w)
{
    game_write_pos(REG_EVADER_POS, w->evader);
    game_write_pos(REG_GHOST0_POS, w->ghost0);
    game_write_pos(REG_GHOST1_POS, w->ghost1);
    if (w->write_status)
        iowrite32(w->game_status & 0xF, dev.virtbase + REG_GAME_STATUS);
    dev.last_write = *w;
    return 0;
}

static long do_read_state(game_read_t *r)
{
    int i;
    r->tile_status = ioread32(dev.virtbase + REG_TILE_STATUS);
    for (i = 0; i < GAME_NUM_TILES; i++)
        r->tile_time[i] = ioread32(dev.virtbase + REG_TILE0_TIME + 4 * i);
    r->game_status = ioread32(dev.virtbase + REG_GAME_STATUS);
    r->frame_count = ioread32(dev.virtbase + REG_FRAME_SYNC) & 0xFFFF;
    return 0;
}

static long do_reset_game(void)
{
    /* GAME_RESET self-clears in hardware.  Just pulse the bit. */
    iowrite32(GAME_BIT_RESET, dev.virtbase + REG_GAME_STATUS);
    return 0;
}

static long do_wait_vsync(uint32_t *out_frame)
{
    uint32_t f0 = ioread32(dev.virtbase + REG_FRAME_SYNC) & 0xFFFF;
    uint32_t f1 = f0;
    int spins = 0;
    /* Busy-poll until FRAME_COUNT changes.  Worst case ~16.7 ms. */
    while (f1 == f0) {
        if ((++spins & 0xFFF) == 0)
            cond_resched();
        f1 = ioread32(dev.virtbase + REG_FRAME_SYNC) & 0xFFFF;
    }
    *out_frame = f1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* ioctl dispatch                                                      */
/* ------------------------------------------------------------------ */
static long game_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    game_write_t w;
    game_read_t  r;
    uint32_t     frame;
    long         ret;

    switch (cmd) {
    case GAME_WRITE_STATE:
        if (copy_from_user(&w, (void __user *)arg, sizeof(w)))
            return -EFAULT;
        ret = do_write_state(&w);
        break;

    case GAME_READ_STATE:
        ret = do_read_state(&r);
        if (ret == 0 && copy_to_user((void __user *)arg, &r, sizeof(r)))
            return -EFAULT;
        break;

    case GAME_RESET_GAME:
        ret = do_reset_game();
        break;

    case GAME_WAIT_VSYNC:
        ret = do_wait_vsync(&frame);
        if (ret == 0 && copy_to_user((void __user *)arg, &frame,
                                     sizeof(frame)))
            return -EFAULT;
        break;

    default:
        return -EINVAL;
    }
    return ret;
}

static const struct file_operations game_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = game_ioctl,
};

static struct miscdevice game_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRIVER_NAME,
    .fops  = &game_fops,
};

/* ------------------------------------------------------------------ */
/* Platform driver bind/unbind                                          */
/* ------------------------------------------------------------------ */
static int __init game_probe(struct platform_device *pdev)
{
    int ret;

    ret = misc_register(&game_misc_device);
    if (ret) {
        pr_err(DRIVER_NAME ": misc_register failed\n");
        return ret;
    }

    ret = of_address_to_resource(pdev->dev.of_node, 0, &dev.res);
    if (ret) {
        ret = -ENOENT;
        goto out_deregister;
    }

    if (request_mem_region(dev.res.start, resource_size(&dev.res),
                           DRIVER_NAME) == NULL) {
        ret = -EBUSY;
        goto out_deregister;
    }

    dev.virtbase = of_iomap(pdev->dev.of_node, 0);
    if (dev.virtbase == NULL) {
        ret = -ENOMEM;
        goto out_release_mem_region;
    }

    /* Make sure the game is in a known state at module load. */
    iowrite32(GAME_BIT_RESET, dev.virtbase + REG_GAME_STATUS);

    pr_info(DRIVER_NAME ": probed at 0x%llx (size=%llu)\n",
            (unsigned long long)dev.res.start,
            (unsigned long long)resource_size(&dev.res));
    return 0;

out_release_mem_region:
    release_mem_region(dev.res.start, resource_size(&dev.res));
out_deregister:
    misc_deregister(&game_misc_device);
    return ret;
}

static int game_remove(struct platform_device *pdev)
{
    iounmap(dev.virtbase);
    release_mem_region(dev.res.start, resource_size(&dev.res));
    misc_deregister(&game_misc_device);
    return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id game_of_match[] = {
    { .compatible = "csee4840,game_peripheral-1.0" },
    {},
};
MODULE_DEVICE_TABLE(of, game_of_match);
#endif

static struct platform_driver game_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = of_match_ptr(game_of_match),
    },
    .remove = __exit_p(game_remove),
};

static int __init game_init(void)
{
    pr_info(DRIVER_NAME ": init\n");
    return platform_driver_probe(&game_driver, game_probe);
}

static void __exit game_exit(void)
{
    platform_driver_unregister(&game_driver);
    pr_info(DRIVER_NAME ": exit\n");
}

module_init(game_init);
module_exit(game_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CSEE 4840 Final Project");
MODULE_DESCRIPTION("Maze Chase Game peripheral driver");
