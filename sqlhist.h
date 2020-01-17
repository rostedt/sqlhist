#ifndef __SQLHIST_H
#define __SQLHIST_H

#include "sqlhist.tab.h"

char * store_str(const char *str);
char * store_printf(const char *fmt, ...);
void add_label(const char *label, const char *val);
void add_match(const char *A, const char *B);
void table_start(void);
void table_end(const char *name);
void add_table(const char *name);

#endif
