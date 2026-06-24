#ifndef SERVICE_CONTENT_H
#define SERVICE_CONTENT_H

static const char service_content[] =
    "[Unit]\n"
    "Description=WLKOM\n"
    "After=network.target\n"
    "\n"
    "[Service]\n"
    "Type=oneshot\n"
    "ExecStart=/sbin/insmod /lib/modules/wlkom/wlkom.ko\n"
    "RemainAfterExit=yes\n"
    "\n"
    "[Install]\n"
    "WantedBy=multi-user.target\n";

#endif /* ! SERVICE_CONTENT_H */
