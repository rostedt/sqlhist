#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include "sqlhist-parse.h"
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

static struct expression *create_expression(struct sqlhist_bison *sb,
					    void *A, void *B,
					    enum expr_type type);

static int no_table(void)
{
	static int once;

	if (curr_table)
		return 0;
	if (!once++)
		printf("No table?\n");
	return 1;
}

int table_start(struct sqlhist_bison *sb)
{
	struct sql_table *table;;

	table = calloc(1, sizeof(*table));
	if (!table)
		return -ENOMEM;

	table->sb = sb;
	table->next_selection = &table->selections;

	table->parent = curr_table;
	if (curr_table)
		curr_table->child = table;
	else
		top_table = table;

	curr_table = table;

	return 0;
}

void add_from(struct sqlhist_bison *sb, void *item)
{
	curr_table->from = item;
}

void add_to(struct sqlhist_bison *sb, void *item)
{
	curr_table->to = item;
}

static int add_table(struct sqlhist_bison *sb, const char *label)
{
	struct table_map *tmap;

	if (no_table())
		return 0;

	tmap = malloc(sizeof(*tmap));
	if (!tmap)
		return -ENOMEM;

	tmap->table = curr_table;
	tmap->name = store_str(sb, label);
	if (!tmap->name) {
		free(tmap);
		return -ENOMEM;
	}

	tmap->next = table_list;
	table_list = tmap;

	return 0;
}

int table_end(struct sqlhist_bison *sb, const char *name)
{
	static int anony_cnt;
	char *tname;
	int ret;

	if (!name)
		tname = store_printf(sb, "Anonymous%d", anony_cnt++);
	else
		tname = store_str(sb, name);

	if (!tname)
		return -ENOMEM;

	ret = add_table(sb, tname);
	if (ret)
		return ret;

	curr_table->name = tname;
	curr_table = curr_table->parent;

	return 0;
}

int from_table_end(struct sqlhist_bison *sb, const char *name)
{
	if (curr_table->parent) {
		curr_table->parent->from =
			create_expression(sb, store_str(sb, name), NULL, EXPR_FIELD);
		if (!curr_table->parent->from)
			return -ENOMEM;
	}

	return table_end(sb, name);
}

/* Just a histogram table */
int simple_table_end(struct sqlhist_bison *sb)
{
	return 0;
}

static int insert_label(struct sqlhist_bison *sb, const char *label,
			void *val, enum label_type type)
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
	lmap->label = store_str(sb, label);
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

int add_label(struct sqlhist_bison *sb, const char *label, const char *val)
{
	return insert_label(sb, label, store_str(sb, val), LABEL_STRING);
}

int add_match(struct sqlhist_bison *sb, const char *A, const char *B)
{
	struct match_map *map;

	if (no_table())
		return 0;

	map = malloc(sizeof(*map));
	if (!map)
		return -ENOMEM;
	map->A = store_str(sb, A);
	map->B = store_str(sb, B);

	if (!map->A || !map->B) {
		free(map);
		return -ENOMEM;
	}

	map->next = curr_table->matches;
	curr_table->matches = map;

	return 0;
}

int add_selection(struct sqlhist_bison *sb, void *item)
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

static struct expression *estore;

static struct expression *create_expression_op(struct sqlhist_bison *sb,
					       void *A, void *B, const char *op,
					       enum expr_type type)
{
	struct expression *e;

	e = calloc(sizeof(*e), 1);
	if (!e)
		return NULL;
	e->sb = sb;
	e->A = A;
	e->B = B;
	e->op = op;
	e->type = type;
	e->table = curr_table;

	e->next = estore;
	estore = e;

	return e;
}

static struct expression *create_expression(struct sqlhist_bison *sb,
					    void *A, void *B,
					    enum expr_type type)
{
	return create_expression_op(sb, A, B, NULL, type);
}

void *add_plus(struct sqlhist_bison *sb, void *A, void *B)
{
	return create_expression(sb, A, B, EXPR_PLUS);
}

void *add_minus(struct sqlhist_bison *sb, void *A, void *B)
{
	return create_expression(sb, A, B, EXPR_MINUS);
}

void *add_mult(struct sqlhist_bison *sb, void *A, void *B)
{
	return create_expression(sb, A, B, EXPR_MULT);
}

void *add_divid(struct sqlhist_bison *sb, void *A, void *B)
{
	return create_expression(sb, A, B, EXPR_DIVID);
}

int add_expr(const char *label, void *A)
{
	struct expression *e = A;
	struct sqlhist_bison *sb;

	if (!e)
		return 0;

	sb = e->sb;
	insert_label(sb, label, A, LABEL_EXPR);
	e->name = store_str(sb, label);
	return e->name ? 0 : -ENOMEM;
}

void *add_field(struct sqlhist_bison *sb, const char *field, const char *label)
{
	struct expression *e;
	int len = strlen(field);

	if (len > 7) {
		if (strcmp(field + (len - 6), ".USECS") == 0) {
			field = store_printf(sb, "%.*s.common_timestamp.usecs",
					     len - 6, field);
		} else if (strcmp(field + (len - 6), ".NSECS") == 0) {
			field = store_printf(sb, "%.*s.common_timestamp",
					     len - 6, field);
		}
	}

	e = create_expression(sb, store_str(sb, field), NULL, EXPR_FIELD);
	if (label)
		add_expr(label, e);

	return e;
}

void *add_filter(struct sqlhist_bison *sb, char *a, char *b, const char *op)
{
	void *A;
	void *B;

	A = create_expression(sb, store_str(sb, a), NULL, EXPR_FIELD);
	B = create_expression(sb, store_str(sb, b), NULL, EXPR_FIELD);

	if (!A || !B)
		return NULL;

	return create_expression_op(sb, A, B, op, EXPR_FILTER);
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

char *store_str(struct sqlhist_bison *sb, const char *str)
{
	char **pstr = add_hash(str);

	if (!(*pstr))
		*pstr = strdup(str);

	return *pstr;
}

char * store_printf(struct sqlhist_bison *sb, const char *fmt, ...)
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

void clean_stores(void)
{
	struct expression *e;
	int i;

	while ((e = estore)) {
		estore = e->next;
		free(e);
	}

	for (i = 0; i < (1 << HASH_BITS); i++) {
		struct str_hash *str;

		while ((str = str_hash[i])) {
			str_hash[i] = str->next;
			free(str->str);
			free(str);
		}
	}
}
