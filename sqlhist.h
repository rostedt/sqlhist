#ifndef __SQLHIST_H
#define __SQLHIST_H

#include "sqlhist.tab.h"

char * store_str(const char *str);
char * store_printf(const char *fmt, ...);
void add_label(const char *label, const char *val);
void dump_label_map(void);

#endif
