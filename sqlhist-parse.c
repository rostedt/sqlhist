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

int table_start(void)
{
	struct sql_table *table;;

	table = calloc(1, sizeof(*table));
	if (!table)
		return -ENOMEM;

	table->next_selection = &table->selections;

	table->parent = curr_table;
	if (curr_table)
		curr_table->child = table;
	else
		top_table = table;

	curr_table = table;

	return 0;
}

void add_from(void *item)
{
	curr_table->from = show_expr(item);
}

void add_to(void *item)
{
	curr_table->to = show_expr(item);
}

static int add_table(const char *label)
{
	struct table_map *tmap;

	if (no_table())
		return 0;

	tmap = malloc(sizeof(*tmap));
	if (!tmap)
		return -ENOMEM;

	tmap->table = curr_table;
	tmap->name = store_str(label);
	if (!tmap->name) {
		free(tmap);
		return -ENOMEM;
	}

	tmap->next = table_list;
	table_list = tmap;

	return 0;
}

int table_end(const char *name)
{
	static int anony_cnt;
	char *tname;
	int ret;

	if (!name)
		tname = store_printf("Anonymous%d", anony_cnt++);
	else
		tname = store_str(name);

	if (!tname)
		return -ENOMEM;

	ret = add_table(tname);
	if (ret)
		return ret;

	curr_table->name = tname;
	curr_table = curr_table->parent;

	return 0;
}

int from_table_end(const char *name)
{
	if (curr_table->parent) {
		curr_table->parent->from = store_str(name);
		if (!curr_table->parent->from)
			return -ENOMEM;
	}

	return table_end(name);
}

/* Just a histogram table */
int simple_table_end(void)
{
	return 0;
}

static int insert_label(const char *label, void *val, enum label_type type)
{
	struct label_map *lmap;
	struct sql_table *table = curr_table;

	if (!table)
		table = top_table;

	if (!table) {
		no_table();
		return 0;
	}

	lmap = malloc(sizeof(*lmap));
	if (!lmap)
		return -ENOMEM;
	lmap->label = store_str(label);
	if (!lmap->label) {
		free(lmap);
		return -ENOMEM;
	}
	lmap->value = val;
	lmap->type = type;

	lmap->next = table->labels;
	table->labels = lmap;
	return 0;
}

int add_label(const char *label, const char *val)
{
	return insert_label(label, store_str(val), LABEL_STRING);
}

int add_match(const char *A, const char *B)
{
	struct match_map *map;

	if (no_table())
		return 0;

	map = malloc(sizeof(*map));
	if (!map)
		return -ENOMEM;
	map->A = store_str(A);
	map->B = store_str(B);

	if (!map->A || !map->B) {
		free(map);
		return -ENOMEM;
	}

	map->next = curr_table->matches;
	curr_table->matches = map;

	return 0;
}

int add_selection(void *item)
{
	struct selection *selection;
	struct expression *e = item;

	if (no_table())
		return 0;

	selection = malloc(sizeof(*selection));
	if (!selection)
		return -ENOMEM;

	selection->item = e;
	selection->name = e->name;
	selection->next = NULL;
	*curr_table->next_selection = selection;
	curr_table->next_selection = &selection->next;

	return 0;
}

const char *show_expr(void *expr)
{
	struct expression *e = expr;

	if (e->name)
		return e->name;

	return __show_expr(expr, false);
}

static struct expression *create_expression_op(void *A, void *B, const char *op,
					       enum expr_type type)
{
	struct expression *e;

	e = calloc(sizeof(*e), 1);
	if (!e)
		return NULL;
	e->A = A;
	e->B = B;
	e->op = op;
	e->type = type;
	e->table = curr_table;

	return e;
}

static struct expression *create_expression(void *A, void *B, enum expr_type type)
{
	return create_expression_op(A, B, NULL, type);
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

int add_expr(const char *label, void *A)
{
	struct expression *e = A;

	if (!e)
		return 0;

	insert_label(label, A, LABEL_EXPR);
	e->name = store_str(label);
	return e->name ? 0 : -ENOMEM;
}

void *add_field(const char *field, const char *label)
{
	struct expression *e;

	e = create_expression(store_str(field), NULL, EXPR_FIELD);
	if (label)
		add_expr(label, e);

	return e;
}

void *add_filter(void *A, void *B, const char *op)
{
	return create_expression_op(A, B, op, EXPR_FILTER);
}

void add_where(void *A)
{
	struct expression *e = A;

	if (curr_table->filter) {
		printf("more than one filter!\n");
		return;
	}
	curr_table->filter = e;
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
