#ifndef __SQLHIST_PARSE_H
#define __SQLHIST_PARSE_H

#include <stdarg.h>

#ifdef HAVE_TRACEFS
#include <tracefs/tracefs.h>
#else
#include "tracefs-stubs.h"
#endif

struct sqlhist_bison {
	void *scanner;
};

#include "sqlhist.tab.h"

#include "sqlhist-defs.h"

char * store_str(struct sqlhist_bison *sb, const char *str);
char * store_printf(struct sqlhist_bison *sb, const char *fmt, ...);
int add_label(struct sqlhist_bison *sb, const char *label, const char *val);
int add_match(struct sqlhist_bison *sb, const char *A, const char *B);
int table_start(struct sqlhist_bison *sb);
int table_end(struct sqlhist_bison *sb, const char *name);
int from_table_end(struct sqlhist_bison *sb, const char *name);
int simple_table_end(struct sqlhist_bison *sb);

const char *show_expr(void *expr);
void *add_plus(struct sqlhist_bison *sb, void *A, void *B);
void *add_minus(struct sqlhist_bison *sb, void *A, void *B);
void *add_mult(struct sqlhist_bison *sb, void *A, void *B);
void *add_divid(struct sqlhist_bison *sb, void *A, void *B);
void *add_field(struct sqlhist_bison *sb, const char *field, const char *label);
void *add_filter(struct sqlhist_bison *sb, char *a, char *b, const char *op);

int add_expr(const char *name, void *expr);
void add_where(void *expr);

int add_selection(struct sqlhist_bison *sb, void *item);
void add_from(struct sqlhist_bison *sb, void *item);
void add_to(struct sqlhist_bison *sb, void *item);

void clean_stores(void);

extern void parse_error(int line, int index, const char *text,
			const char *fmt, va_list ap);

extern struct sql_table *curr_table;
extern struct sql_table *top_table;
extern struct table_map *table_list;

#endif
