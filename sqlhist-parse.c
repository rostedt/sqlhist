#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include "sqlhist.h"
#include "sqlhist-local.h"

void die(const char *fmt, ...);

struct sql_table *curr_table;
struct sql_table *top_table;
struct table_map *table_list;

#define HASH_BITS 10

struct str_hash {
	struct str_hash		*next;
	char			*str;
};

static struct str_hash *str_hash[1 << HASH_BITS];

static int no_table(void)
{
	static int once;

	if (curr_table)
		return 0;
	if (!once++)
		printf("No table?\n");
	return 1;
}

void table_start(void)
{
	struct sql_table *table;;

	table = calloc(1, sizeof(*table));
	if (!table)
		die("malloc");

	table->next_selection = &table->selections;

	table->parent = curr_table;
	if (curr_table)
		curr_table->child = table;
	else
		top_table = table;

	curr_table = table;
}

void add_from(void *item)
{
	curr_table->from = show_expr(item);
}

void add_to(void *item)
{
	curr_table->to = show_expr(item);
}

static void add_table(const char *label)
{
	struct table_map *tmap;

	if (no_table())
		return;

	tmap = malloc(sizeof(*tmap));
	if (!tmap)
		die("malloc");

	tmap->table = curr_table;
	tmap->name = store_str(label);

	tmap->next = table_list;
	table_list = tmap;
}

void table_end(const char *name)
{
	static int anony_cnt;
	char *tname;

	if (!name)
		tname = store_printf("Anonymous%d", anony_cnt++);
	else
		tname = store_str(name);

	add_table(tname);

	curr_table->name = tname;
	curr_table = curr_table->parent;
}

void from_table_end(const char *name)
{
	if (curr_table->parent)
		curr_table->parent->from = store_str(name);

	table_end(name);
}

/* Just a histogram table */
void simple_table_end(void)
{

}

static void insert_label(const char *label, void *val, enum label_type type)
{
	struct label_map *lmap;
	struct sql_table *table = curr_table;

	if (!table)
		table = top_table;

	if (!table) {
		no_table();
		return;
	}

	lmap = malloc(sizeof(*lmap));
	if (!lmap)
		die("malloc");
	lmap->label = store_str(label);
	lmap->value = val;
	lmap->type = type;

	lmap->next = table->labels;
	table->labels = lmap;
}

void add_label(const char *label, const char *val)
{
	insert_label(label, store_str(val), LABEL_STRING);
}

void add_match(const char *A, const char *B)
{
	struct match_map *map;

	if (no_table())
		return;

	map = malloc(sizeof(*map));
	if (!map)
		die("malloc");
	map->A = store_str(A);
	map->B = store_str(B);

	map->next = curr_table->matches;
	curr_table->matches = map;
}

void add_selection(void *item)
{
	struct selection *selection;
	struct expression *e = item;

	if (no_table())
		return;

	selection = malloc(sizeof(*selection));
	if (!selection)
		die("malloc");

	selection->item = e;
	selection->name = e->name;
	selection->next = NULL;
	*curr_table->next_selection = selection;
	curr_table->next_selection = &selection->next;
}

const char *show_expr(void *expr)
{
	struct expression *e = expr;

	if (e->name)
		return e->name;

	return __show_expr(expr, false);
}

static struct expression *create_expression(void *A, void *B, enum expr_type type)
{
	struct expression *e;

	e = calloc(sizeof(*e), 1);
	if (!e)
		die("malloc");
	e->A = A;
	e->B = B;
	e->type = type;
	e->table = curr_table;

	return e;
}

void *add_plus(void *A, void *B)
{
	return create_expression(A, B, EXPR_PLUS);
}

void *add_minus(void *A, void *B)
{
	return create_expression(A, B, EXPR_MINUS);
}

void *add_mult(void *A, void *B)
{
	return create_expression(A, B, EXPR_MULT);
}

void *add_divid(void *A, void *B)
{
	return create_expression(A, B, EXPR_DIVID);
}

void add_expr(const char *label, void *A)
{
	struct expression *e = A;

	insert_label(label, A, LABEL_EXPR);
	e->name = store_str(label);
}

void *add_field(const char *field, const char *label)
{
	struct expression *e;

	e = create_expression(store_str(field), NULL, EXPR_FIELD);
	if (label)
		add_expr(label, e);

	return e;
}

static inline unsigned int quick_hash(const char *str)
{
	unsigned int val = 0;
	int len = strlen(str);

	for (; len >= 4; str += 4, len -= 4) {
		val += str[0];
		val += str[1] << 8;
		val += str[2] << 16;
		val += str[3] << 24;
	}
	for (; len > 0; str++, len--)
		val += str[0] << (len * 8);

        val *= 2654435761;

        return val & ((1 << HASH_BITS) - 1);
}


static struct str_hash *find_string(const char *str)
{
	unsigned int key = quick_hash(str);
	struct str_hash *hash;

	for (hash = str_hash[key]; hash; hash = hash->next) {
		if (!strcmp(hash->str, str))
			return hash;
	}
	return NULL;
}

/*
 * If @str is found, then return the hash string.
 * This lets store_str() know to free str.
 */
static char **add_hash(const char *str)
{
	struct str_hash *hash;
	unsigned int key;

	if ((hash = find_string(str))) {
		return &hash->str;
	}

	hash = malloc(sizeof(*hash));
	key = quick_hash(str);
	hash->next = str_hash[key];
	str_hash[key] = hash;
	hash->str = NULL;
	return &hash->str;
}

char *store_str(const char *str)
{
	char **pstr = add_hash(str);

	if (!(*pstr))
		*pstr = strdup(str);

	return *pstr;
}

char * store_printf(const char *fmt, ...)
{
	va_list ap;
	char **pstr;
	char *str;
	int ret;

	va_start(ap, fmt);
	ret = vasprintf(&str, fmt, ap);
	va_end(ap);

	if (!ret)
		return NULL; /* ?? */

	pstr = add_hash(str);
	if (*pstr)
		free(str);
	else
		*pstr = str;

	return *pstr;
}
