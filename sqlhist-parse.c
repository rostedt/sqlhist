#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include "sqlhist.h"

/*
 *  Ideally, we will conevert:
 *
 * trace-cmd start						\
 *    --sql '(select start.common_timestamp as start_time,
 *                    end.common_timestamp as end_time, start.pid,
 *                   (start_time - end_time) as delta
 *            from sched_waking as start
 *            join sched_switch as end
 *              on start.pid = end.next_pid) as first'
 *
 * to this:
 *
 * # echo 'first u64 start_time u64 end_time pid_t pid u64 delta' >> synthetic_events 
 * # echo 'hist:keys=pid:start=common_timestamp' > 
 *                      events/sched/sched_waking/trigger 
 * # echo 'hist:keys=next_pid:start2=$start,delta=common_timestamp-$start:onmatch(sched.sched_waking).trace(first,$start2,common_timestamp,next_pid,$delta)' >
 *                       events/sched/sched_switch/trigger
 */

void die(const char *fmt, ...);

#define HASH_BITS 10

struct str_hash {
	struct str_hash		*next;
	char			*str;
};

static struct str_hash *str_hash[1 << HASH_BITS];

enum label_type {
	LABEL_STRING,
	LABEL_EXPR,
};

struct label_map {
	struct label_map	*next;
	enum label_type		type;
	char			*label;
	void			*value;
};

struct match_map {
	struct match_map	*next;
	const char		*A;
	const char		*B;
};

struct selection {
	struct selection	*next;
	const char		*name;
	void			*item;
};

enum expr_type {
	EXPR_FIELD,
	EXPR_PLUS,
	EXPR_MINUS,
	EXPR_MULT,
	EXPR_DIVID,
};

struct sql_table;

struct expression {
	enum expr_type		type;
	void			*A;
	void			*B;
	const char		*name;
	struct sql_table	*table;
};

struct table_map {
	struct table_map	*next;
	char			*name;
	struct sql_table	*table;
};

struct sql_table {
	char			*name;
	struct sql_table	*parent;
	struct sql_table	*child;
	struct label_map	*labels;
	struct match_map	*matches;
	struct table_map	*tables;
	struct selection	*selections;
	struct selection	**next_selection;
	const char		*from;
	const char		*to;
};

static struct sql_table *curr_table;
static struct sql_table *top_table;
static struct table_map *table_list;

static struct tep_handle *tep;

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

static struct sql_table *find_table(const char *name)
{
	struct table_map *tmap;

	for (tmap = table_list; tmap; tmap = tmap->next)
		if (strcmp(tmap->name, name) == 0)
			return tmap->table;
	return NULL;
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

static char *expr_op_connect(void *A, void *B, char *op,
			     const char *(*show)(void *A))
{
	struct expression *eA = A;
	struct expression *eB = B;
	char *a = NULL, *b = NULL;
	char *ret, *str;
	int r;

	if (eA->name) {
		r = asprintf(&a, "%s AS %s", show(A), eA->name);
		if (r < 0)
			die("asprintf");
	}

	if (eB->name) {
		r = asprintf(&b, "%s AS %s", show(B), eB->name);
		if (r < 0)
			die("asprintf");
	}

	r = asprintf(&str, "(%s %s %s)",
		     a ? a : show(A), op, b ? b : show(B));
	if (r < 0)
		die("asprintf");
	free(a);
	free(b);

	ret = store_str(str);
	free(str);
	return ret;
}

static const char *show_raw_expr(void *e);
static const char *resolve(struct sql_table *table, const char *label);

static const char *expand(const char *str)
{
	char *exp = strdup(str);
	const char *label;
	const char *ret;
	char *p;

	if ((p = strstr(exp, "."))) {
		*p = 0;
		label = resolve(curr_table, exp);
		ret = store_printf("%s.%s", label, p+1);
		*p = '.';
	} else {
		ret = resolve(curr_table, str);
	}
	free(exp);
	return ret;
}

static const char *__show_expr(struct expression *e, bool eval)
{
	const char *(*show)(void *);
	char *ret;

	if (eval)
		show = show_raw_expr;
	else
		show = show_expr;

	switch(e->type) {
	case EXPR_FIELD:
		ret = e->A;
		if (eval)
			return expand(e->A);
		break;
	case EXPR_PLUS:
		ret = expr_op_connect(e->A, e->B, "+", show);
		break;
	case EXPR_MINUS:
		ret = expr_op_connect(e->A, e->B, "-", show);
		break;
	case EXPR_MULT:
		ret = expr_op_connect(e->A, e->B, "*", show);
		break;
	case EXPR_DIVID:
		ret = expr_op_connect(e->A, e->B, "/", show);
		break;
	}
	return ret;
}

static const char *show_raw_expr(void *e)
{
	return __show_expr(e, true);
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

static void usage(char **argv)
{
	char *arg = argv[0];
	char *p = arg+strlen(arg);

	while (p >= arg && *p != '/')
		p--;
	p++;

	printf("\nusage: %s [-hl][-t tracefs-path][file]\n"
	       " file : holds sql statement (read from stdin if not present)\n"
	       " -h : show this message\n"
	       " -l : Only run the lexer (for testing)\n"
	       " -t : Path to tracefs directory (looks for it via /proc/mounts if not set)\n"
	       "\n",p);
	exit(-1);
}

static void __vdie(const char *fmt, va_list ap, int err)
{
	int ret = errno;

	if (err && errno)
		perror("bmp-read");
	else
		ret = -1;

	fprintf(stderr, "  ");
	vfprintf(stderr, fmt, ap);

	fprintf(stderr, "\n");
	exit(ret);
}

void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vdie(fmt, ap, 0);
	va_end(ap);
}

void pdie(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vdie(fmt, ap, 1);
	va_end(ap);
}

extern int yylex(void);
extern char *yytext;

static int lex_it(void)
{
	int ret;

	do {
		ret = yylex();
	} while (ret > 0);

	return ret;
}

static void dump_label_map(struct sql_table *table)
{
	struct label_map *lmap;
	struct table_map *tmap;

	if (table->labels)
		printf("%s Labels:\n", table->name);
	for (lmap = table->labels; lmap; lmap = lmap->next) {
		switch (lmap->type) {
		case LABEL_STRING:
			printf("  %s = %s\n",
			       lmap->label, (char *)lmap->value);
			break;
		case LABEL_EXPR:
			printf("  %s = (%s)\n", lmap->label,
			       show_raw_expr(lmap->value));
			break;
		}
	}
	if (table->tables)
		printf("%s Tables:\n", table->name);
	for (tmap = table->tables; tmap; tmap = tmap->next) {
		printf("  %s = Table %s\n", tmap->name, tmap->table->name);
	}
}

static void dump_match_map(struct sql_table *table)
{
	struct match_map *map;

	if (table->matches)
		printf("%s Matches:\n", table->name);
	for (map = table->matches; map; map = map->next) {
		printf("  %s = %s\n", map->A, map->B);
	}
}

static void dump_table(struct sql_table *table)
{
	struct sql_table *save_curr = curr_table;

	if (!table)
		return;

	dump_table(find_table(table->from));

	curr_table = table;

	printf("\nTable: %s\n", table->name);
	dump_label_map(table);
	dump_match_map(table);

	curr_table = save_curr;

	dump_table(find_table(table->to));
}

static const char *event_match(const char *event, const char *val, int len)
{
	if (strncmp(event, val, len) == 0 && val[len] == '.')
		return val + len + 1;

	return NULL;
}

static char * make_dynamic_arg(void)
{
	static int arg_cnt;

	return store_printf("__arg%d__", arg_cnt++);
}

static struct tep_event *find_event(struct tep_handle *tep, const char *name)
{
	static struct tep_event stub_event = {
		.system			= "(system)",
	};

	if (tep)
		return tep_find_event_by_name(tep, NULL, name);

	return &stub_event;
}

static struct tep_format_field *find_field(struct tep_event *event, char *name)
{
	static struct tep_format_field stub_field = {
		.type = "(unknown)",
	};

	if (tep)
		return tep_find_any_field(event, name);

	return &stub_field;
}

static void print_type(struct expression *e)
{
	struct tep_format_field *field;
	struct tep_event *event;
	char *name;
	char *tok;

	while (e && e->type != EXPR_FIELD) {
		e = e->A;
	}

	if (!e) {
		printf(" (unknown-expression) ");
		return;
	}

	name = strdup(show_raw_expr(e));

	tok = strtok(name, ".");

	event = find_event(tep, tok);
	if (!event) {
		tok = strtok(NULL, ".");
		if (!tok)
			goto out;
		event = find_event(tep, tok);
	}

	tok = strtok(NULL, ".");
	if (!tok || !event)
		goto out;

	if (strcmp(tok, "common_timestamp") == 0) {
		printf(" u64 ");
	} else {
		field = find_field(event, tok);
		if (field)
			printf(" %s ", field->type);
		else
			printf(" (no-field-%s-for-%s) ", tok, name);
	}
 out:
	if (!event)
		printf(" (no-event-for:%s) ", name);
	free(name);
}

static void print_synthetic_field(struct sql_table *table, struct selection *selection)
{
	struct expression *e = selection->item;
	const char *name;
	const char *actual;
	const char *field;
	const char *to;
	int len;

	print_type(e);

	name = selection->name;
	if (!name)
		name = e->name;
	if (name) {
		printf("%s", name);
		return;
	}

	to = resolve(table, table->to);
	len = strlen(to);

	actual = show_raw_expr(e);
	field = event_match(to, actual, len);
	if (field) {
		printf("%s", field);
		return;
	}

	selection->name = make_dynamic_arg();
	e->name = selection->name;

	field = strstr(actual, ".");
	if (field) {
		/* Need to check for common_timestamp */
		printf("%s", field + 1);
	} else {
		printf("%s", e->name);
	}
}

static void make_synthetic_events(struct sql_table *table)
{
	struct selection *selection;
	struct sql_table *save_curr = curr_table;

	if (!table || !table->to)
		return;

	make_synthetic_events(find_table(table->from));

	curr_table = table;

	printf("echo '%s", table->name);
	for (selection = table->selections; selection; selection = selection->next)
		print_synthetic_field(table, selection);

	printf("' > synthetic_events\n");

	curr_table = save_curr;

	make_synthetic_events(find_table(table->to));
}

static const char *resolve(struct sql_table *table, const char *label)
{
	struct sql_table *save_curr = curr_table;
	struct label_map *lmap;
	struct expression *e;

	curr_table = table;

	for (lmap = table->labels; lmap; lmap = lmap->next)
		if (strcmp(lmap->label, label) == 0)
			break;

	if (lmap) {
		switch (lmap->type) {
		case LABEL_STRING:
			label = (char *)lmap->value;
			break;
		case LABEL_EXPR:
			e = lmap->value;
			label = show_raw_expr(e);
			break;
		}
	}

	curr_table = save_curr;

	return label;
}

static void print_key(struct sql_table *table,
		      const char *event,
		      const char *key1, const char *key2)
{
	int len = strlen(event);
	const char *field;

	field = event_match(event, key1, len);
	if (field) 
		printf("%s", field);

	field = event_match(event, key2, len);
	if (field)
		printf("%s", field);
}

static void print_keys(struct sql_table *table, const char *event)
{
	struct selection *selection;
	struct match_map *map;
	struct expression *e;
	char *f, *p;
	int start = 0;

	if (event) {
		f = strdup(event);
		if ((p = strstr(f, ".")))
			*p = '\0';

		for (map = table->matches; map; map = map->next) {
			if (start++)
				printf(",");
			print_key(table, f, expand(map->A), expand(map->B));
		}

		free(f);
	} else {
		for (selection = table->selections; selection; selection = selection->next) {
			e = selection->item;
			if (!e->name || strncmp(e->name, "key", 3) != 0)
				continue;
			if (start++)
				printf(",");
			printf("%s", show_raw_expr(e));
		}
	}
}

enum value_type {
	VALUE_TO,
	VALUE_FROM,
};

static void print_val_delim(bool *start)
{
	if (*start) {
		printf(":");
		*start = false;
	} else
		printf(",");
}

struct var_list {
	struct var_list		*next;
	const char		*var;
	const char		*val;
};

static const char *find_var(struct var_list **vars, const char *val)
{
	struct var_list *v;
	
	for (v = *vars; v; v = v->next) {
		if (strcmp(v->val, val) == 0)
		    return v->var;
	}

	return NULL;
}

static void add_var(struct var_list **vars, const char *var, const char *val)
{
	struct var_list *v;

	v = malloc(sizeof(*v));
	if (!v)
		die("malloc");
	v->var = var;
	v->val = val;
	v->next = *vars;
	*vars = v;
}

static void print_to_expr(struct sql_table *table, const char *event,
			  struct expression *e,
			  struct var_list **vars)
{
	const char *actual;
	const char *field;
	int len = strlen(event);

	switch (e->type) {
	case EXPR_FIELD:
		actual = show_raw_expr(e);
		field = event_match(event, actual, len);
		if (field) {
			printf("%s", field);
			break;
		}

		field = strstr(actual, ".");
		if (!field) {
			printf("%s", field);
			break;
		}
		printf("$%s", find_var(vars, actual));
		break;
	default:
		print_to_expr(table, event, e->A, vars);
		switch (e->type) {
		case EXPR_PLUS:		printf("+");	break;
		case EXPR_MINUS:	printf("-");	break;
		case EXPR_MULT:		printf("*");	break;
		case EXPR_DIVID:	printf("/");	break;
		default: break;
		}
		print_to_expr(table, event, e->B, vars);
	}
}

static void print_from_expr(struct sql_table *table, const char *event,
			    struct expression *e, bool *start,
			    struct var_list **vars)
{
	const char *actual;
	const char *field;
	int len = strlen(event);

	switch (e->type) {
	case EXPR_FIELD:
		actual = show_raw_expr(e);
		field = event_match(event, actual, len);
		if (field && !find_var(vars, actual)) {
			print_val_delim(start);
			if (!e->name)
				e->name = make_dynamic_arg();
			printf("%s=%s", e->name, field);
			add_var(vars, e->name, actual);
			break;
		}
		break;
	default:
		print_from_expr(table, event, e->A, start, vars);
		print_from_expr(table, event, e->B, start, vars);
	}
}

static void print_value(struct sql_table *table,
			const char *event, struct selection *selection,
			enum value_type type, bool *start, struct var_list **vars)
{
	struct expression *e = selection->item;
	const char *name = selection->name;
	int len = strlen(event);
	const char *actual;
	const char *field;

	switch (e->type) {
	case EXPR_FIELD:
		if (!selection->name || !e->name)
			break;
		actual = show_raw_expr(e);
		field = event_match(event, actual, len);
		if (field) {
			if (type != VALUE_TO) {
				print_val_delim(start);
				printf("%s=%s", e->name, field);
				add_var(vars, e->name, actual);
			}
		} else if (type == VALUE_TO) {
			const char *arg;
			/*
			 * The selection wants to show the from,
			 * We need to save the from field in a variable
			 */
			print_val_delim(start);
			arg = make_dynamic_arg();
			printf("%s=$%s", arg, e->name);
			add_var(vars, arg, actual);
			/* Now use the this arg for the parameters */
			selection->name = arg;
		}
		break;
	default:
		if (type == VALUE_TO) {
			print_val_delim(start);
			printf("%s=", name);
			print_to_expr(table, event, e, vars);
		} else {
			print_from_expr(table, event, e, start, vars);
		}
		break;
	}

}

static void print_values(struct sql_table *table, const char *event,
			 enum value_type type, struct var_list **vars)
{
	struct selection *selection;
	struct expression *e;
	char *f, *p;
	bool start = true;

	if (event) {
		f = strdup(event);
		if ((p = strstr(f, ".")))
			*p = '\0';

		for (selection = table->selections; selection; selection = selection->next) {
			print_value(table, f, selection, type, &start, vars);
		}
		free(f);
	} else {
		for (selection = table->selections; selection; selection = selection->next) {
			e = selection->item;
			if (e->name && strncmp(e->name, "key", 3) == 0)
				continue;
			if (start) {
				printf(":values=");
				start = false;
			} else {
				printf(",");
			}
			printf("%s", show_raw_expr(e));
		}
	}
}

static void print_trace_field(struct sql_table *table, struct selection *selection)
{
	struct expression *e = selection->item;
	const char *name;
	const char *actual;
	const char *field;
	const char *to;
	int len;

	to = resolve(table, table->to);
	len = strlen(to);

	actual = show_raw_expr(e);
	field = event_match(to, actual, len);
	if (field) {
		printf(",%s", field);
		return;
	}

	name = selection->name;
	if (!name)
		name = e->name;
	if (name) {
		printf(",$%s", name);
		return;
	}

	printf(",ERROR");
}

static void print_trace(struct sql_table *table)
{
	struct selection *selection;

	printf(".trace(%s", table->name);

	for (selection = table->selections; selection; selection = selection->next)
		print_trace_field(table, selection);
	printf(")");
}

static void print_system_event(const char *text, char delim)
{
	struct tep_event *event;
	char *name = strdup(text);
	char *tok;

	strtok(name, ".");
	tok = strtok(NULL, ".");
	if (tok) {
		printf("%s%c%s", tok, delim, name);
		goto out;
	}

	event = find_event(tep, name);
	if (!event) {
		printf("(system)%c%s", delim, name);
		goto out;
	}

	printf("%s%c%s", event->system, delim, name);

 out:
	free(name);
}

static void make_histograms(struct sql_table *table)
{
	struct sql_table *save_curr = curr_table;
	struct var_list *vars = NULL;
	const char *from = NULL;
	const char *to;

	if (!table)
		return;

	/* Need to do children and younger siblings first */
	make_histograms(find_table(table->from));

	curr_table = table;

	if (table->to)
		from = resolve(table, table->from);

	printf("echo 'hist:keys=");
	print_keys(table, from);
	print_values(table, from, VALUE_FROM, &vars);

	if (!table->to)
		from = resolve(table, table->from);
	printf("' > events/");
	print_system_event(from, '/');
	printf("/trigger\n");

	if (!table->to)
		goto out;

	printf("echo 'hist:keys=");
	to = resolve(table, table->to);
	print_keys(table, to);
	print_values(table, to, VALUE_TO, &vars);
	printf(":onmatch(");
	print_system_event(from, '.');
	printf(")");

	print_trace(table); 
	printf("' > events/");
	print_system_event(to, '/');
	printf("/trigger\n");

	while (vars) {
		struct var_list *v = vars;
		vars = v->next;
		free(v);
	}

 out:
	curr_table = save_curr;

	make_histograms(find_table(table->to));
}

static void dump_tables(void)
{
	static int debug;

	if (!debug)
		return;

	dump_table(curr_table);
}

static int parse_it(void)
{
	int ret;

	ret = yyparse();

	dump_tables();

	return ret;
}

static char *buffer;
static size_t buffer_size;
static size_t buffer_idx;

int my_yyinput(char *buf, int max)
{
	if (!buffer)
		return read(0, buf, max);
	
	if (buffer_idx + max > buffer_size)
		max = buffer_size - buffer_idx;

	if (max)
		memcpy(buf, buffer + buffer_idx, max);

	buffer_idx += max;
	
	return max;
}

int main (int argc, char **argv)
{
	char *trace_dir = NULL;
	char buf[BUFSIZ];
	FILE *fp;
	size_t r;
	int c;

	for (;;) {
		c = getopt(argc, argv, "hlt:");
		if (c == -1)
			break;

		switch(c) {
		case 'h':
			usage(argv);
		case 'l':
			return lex_it();
		case 't':
			trace_dir = optarg;
			break;
		}
	}
	if (argc - optind > 0) {

		fp = fopen(argv[optind], "r");
		while ((r = fread(buf, 1, BUFSIZ, fp)) > 0) {
			buffer = realloc(buffer, buffer_size + r + 1);
			strncpy(buffer + buffer_size, buf, r);
			buffer_size += r;
		}
		fclose(fp);
		if (buffer_size)
			buffer[buffer_size] = '\0';
	}

	parse_it();

	tep = tracefs_local_events(trace_dir);
	if (!tep)
		perror("failed to read local events ");

	printf("\n");
	make_synthetic_events(top_table);
	printf("\n");
	make_histograms(top_table);

	return 0;
}
