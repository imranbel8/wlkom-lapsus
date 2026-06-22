#include <linux/module.h>
#include <linux/kernel.h>
#include "persist.h"

int persist_init(void)
{
    pr_info("WLKOM persist: ready\n");
    return 0;
}

void persist_exit(void)
{
}
