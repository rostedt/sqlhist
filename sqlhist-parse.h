#ifndef __SQLHIST_PARSE_H
#define __SQLHIST_PARSE_H

#include <stdarg.h>

#ifdef HAVE_TRACEFS
#include <tracefs/tracefs.h>
#else
#include "tracefs-stubs.h"
#endif

#include "sqlhist.tab.h"

#include "sqlhist-defs.h"

char * store_str(const char *str);
char * store_printf(const char *fmt, ...);
int add_label(const char *label, const char *val);
int add_match(const char *A, const char *B);
int table_start(void);
int table_end(const char *name);
int from_table_end(const char *name);
int simple_table_end(void);

const char *show_expr(void *expr);
void *add_plus(void *A, void *B);
void *add_minus(void *A, void *B);
void *add_mult(void *A, void *B);
void *add_divid(void *A, void *B);
void *add_field(const char *field, const char *label);
void *add_filter(char *a, char *b, const char *op);

int add_expr(const char *name, void *expr);
void add_where(void *expr);

int add_selection(void *item);
void add_from(void *item);
void add_to(void *item);

void clean_stores(void);

extern void parse_error(int line, int index, const char *text,
			const char *fmt, va_list ap);

extern struct sql_table *curr_table;
extern struct sql_table *top_table;
extern struct table_map *table_list;

#endif
