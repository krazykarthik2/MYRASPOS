#ifndef SERVICE_H
#define SERVICE_H

#include <stddef.h>

int services_init(void);
int services_load_all(void);
int service_load_unit(const char *path);
int service_start(const char *name);
int service_stop(const char *name);
int service_restart(const char *name);
int service_reload(const char *name); /* NULL name means reload all */
int service_enable(const char *name);
int service_disable(const char *name);
int service_status(const char *name, char *buf, size_t len);

#endif
