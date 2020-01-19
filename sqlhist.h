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

const char *show_expr(void *expr);
void *add_plus(void *A, void *B);
void *add_minus(void *A, void *B);
void *add_mult(void *A, void *B);
void *add_divid(void *A, void *B);
void *add_field(const char *field, const char *label);

void add_expr(const char *name, void *expr);

void add_selection(void *item);

#endif
