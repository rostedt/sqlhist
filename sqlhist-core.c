#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "sqlhist-parse.h"
#include "sqlhist-local.h"

#include <trace-seq.h>

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

static struct tep_handle *tep;

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

static const char *show_raw_expr(void *e)
{
	return __show_expr(e, true);
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

static char *expr_op_connect(void *A, void *B, const char *op,
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
			return NULL;
	}

	if (eB->name) {
		r = asprintf(&b, "%s AS %s", show(B), eB->name);
		if (r < 0)
			return NULL;
	}

	r = asprintf(&str, "(%s %s %s)",
		     a ? a : show(A), op, b ? b : show(B));
	if (r < 0)
		return NULL;
	free(a);
	free(b);

	ret = store_str(str);
	free(str);
	return ret;
}

const char *__show_expr(struct expression *e, bool eval)
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
	case EXPR_FILTER:
		ret = expr_op_connect(e->A, e->B, e->op, show);
		break;
	}
	return ret;
}

static struct sql_table *find_table(const char *name)
{
	struct table_map *tmap;

	for (tmap = table_list; tmap; tmap = tmap->next)
		if (strcmp(tmap->name, name) == 0)
			return tmap->table;
	return NULL;
}

static void dump_label_map(struct trace_seq *s, struct sql_table *table)
{
	struct label_map *lmap;
	struct table_map *tmap;

	if (table->labels)
		trace_seq_printf(s, "%s Labels:\n", table->name);
	for (lmap = table->labels; lmap; lmap = lmap->next) {
		switch (lmap->type) {
		case LABEL_STRING:
			trace_seq_printf(s, "  %s = %s\n",
			       lmap->label, (char *)lmap->value);
			break;
		case LABEL_EXPR:
			trace_seq_printf(s, "  %s = (%s)\n", lmap->label,
			       show_raw_expr(lmap->value));
			break;
		}
	}
	if (table->tables)
		trace_seq_printf(s, "%s Tables:\n", table->name);
	for (tmap = table->tables; tmap; tmap = tmap->next) {
		trace_seq_printf(s, "  %s = Table %s\n", tmap->name, tmap->table->name);
	}
}

static void dump_match_map(struct trace_seq *s, struct sql_table *table)
{
	struct match_map *map;

	if (table->matches)
		trace_seq_printf(s, "%s Matches:\n", table->name);
	for (map = table->matches; map; map = map->next) {
		trace_seq_printf(s, "  %s = %s\n", map->A, map->B);
	}
}

static void dump_table(struct trace_seq *s, struct sql_table *table)
{
	struct sql_table *save_curr = curr_table;

	if (!table)
		return;

	dump_table(s, find_table(table->from));

	curr_table = table;

	trace_seq_printf(s, "\nTable: %s\n", table->name);
	dump_label_map(s, table);
	dump_match_map(s, table);

	curr_table = save_curr;

	dump_table(s, find_table(table->to));
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

static void print_type(struct trace_seq *s, struct expression *e)
{
	struct tep_format_field *field;
	struct tep_event *event;
	char *name;
	char *tok;

	while (e && e->type != EXPR_FIELD) {
		e = e->A;
	}

	if (!e) {
		trace_seq_printf(s, " (unknown-expression) ");
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
		trace_seq_printf(s, " u64 ");
	} else {
		field = find_field(event, tok);
		if (field)
			trace_seq_printf(s, " %s ", field->type);
		else
			trace_seq_printf(s, " (no-field-%s-for-%s) ", tok, name);
	}
 out:
	if (!event)
		trace_seq_printf(s, " (no-event-for:%s) ", name);
	free(name);
}

static void print_synthetic_field(struct trace_seq *s,
				  struct sql_table *table,
				  struct selection *selection)
{
	struct expression *e = selection->item;
	const char *name;
	const char *actual;
	const char *field;
	const char *to;
	int len;

	print_type(s, e);

	name = selection->name;
	if (!name)
		name = e->name;
	if (name) {
		trace_seq_printf(s, "%s", name);
		return;
	}

	to = resolve(table, table->to);
	len = strlen(to);

	actual = show_raw_expr(e);
	field = event_match(to, actual, len);
	if (field) {
		trace_seq_printf(s, "%s", field);
		return;
	}

	selection->name = make_dynamic_arg();
	e->name = selection->name;

	field = strstr(actual, ".");
	if (field) {
		/* Need to check for common_timestamp */
		trace_seq_printf(s, "%s", field + 1);
	} else {
		trace_seq_printf(s, "%s", e->name);
	}
}

static void make_synthetic_events(struct trace_seq *s, struct sql_table *table)
{
	struct selection *selection;
	struct sql_table *save_curr = curr_table;

	if (!table || !table->to)
		return;

	make_synthetic_events(s, find_table(table->from));

	curr_table = table;

	trace_seq_printf(s, "%s", table->name);
	for (selection = table->selections; selection; selection = selection->next)
		print_synthetic_field(s, table, selection);

	curr_table = save_curr;

	make_synthetic_events(s, find_table(table->to));
}

static void print_key(struct trace_seq *s,
		      struct sql_table *table,
		      const char *event,
		      const char *key1, const char *key2)
{
	int len = strlen(event);
	const char *field;

	field = event_match(event, key1, len);
	if (field) 
		trace_seq_printf(s, "%s", field);

	field = event_match(event, key2, len);
	if (field)
		trace_seq_printf(s, "%s", field);
}

static void print_keys(struct trace_seq *s, struct sql_table *table, const char *event)
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
				trace_seq_printf(s, ",");
			print_key(s, table, f, expand(map->A), expand(map->B));
		}

		free(f);
	} else {
		for (selection = table->selections; selection; selection = selection->next) {
			e = selection->item;
			if (!e->name || strncmp(e->name, "key", 3) != 0)
				continue;
			if (start++)
				trace_seq_printf(s, ",");
			trace_seq_printf(s, "%s", show_raw_expr(e));
		}
	}
}

enum value_type {
	VALUE_TO,
	VALUE_FROM,
};

static void print_val_delim(struct trace_seq *s, bool *start)
{
	if (*start) {
		trace_seq_printf(s, ":");
		*start = false;
	} else
		trace_seq_printf(s, ",");
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

static int add_var(struct var_list **vars, const char *var, const char *val)
{
	struct var_list *v;

	v = malloc(sizeof(*v));
	if (!v)
		return -ENOMEM;
	v->var = var;
	v->val = val;
	v->next = *vars;
	*vars = v;

	return 0;
}

static void print_to_expr(struct trace_seq *s,
			  struct sql_table *table, const char *event,
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
			trace_seq_printf(s, "%s", field);
			break;
		}

		field = strstr(actual, ".");
		if (!field) {
			trace_seq_printf(s, "%s", field);
			break;
		}
		trace_seq_printf(s, "$%s", find_var(vars, actual));
		break;
	default:
		print_to_expr(s, table, event, e->A, vars);
		switch (e->type) {
		case EXPR_PLUS:		trace_seq_printf(s, "+");	break;
		case EXPR_MINUS:	trace_seq_printf(s, "-");	break;
		case EXPR_MULT:		trace_seq_printf(s,"*");	break;
		case EXPR_DIVID:	trace_seq_printf(s, "/");	break;
		default: break;
		}
		print_to_expr(s, table, event, e->B, vars);
	}
}

static int print_from_expr(struct trace_seq *s,
			   struct sql_table *table, const char *event,
			    struct expression *e, bool *start,
			    struct var_list **vars)
{
	const char *actual;
	const char *field;
	int len = strlen(event);
	int ret = 0;

	switch (e->type) {
	case EXPR_FIELD:
		actual = show_raw_expr(e);
		field = event_match(event, actual, len);
		if (field && !find_var(vars, actual)) {
			print_val_delim(s, start);
			if (!e->name)
				e->name = make_dynamic_arg();
			trace_seq_printf(s, "%s=%s", e->name, field);
			ret = add_var(vars, e->name, actual);
			break;
		}
		break;
	default:
		print_from_expr(s, table, event, e->A, start, vars);
		print_from_expr(s, table, event, e->B, start, vars);
	}
	return ret;
}

static int print_value(struct trace_seq *s,
		       struct sql_table *table,
			const char *event, struct selection *selection,
			enum value_type type, bool *start, struct var_list **vars)
{
	struct expression *e = selection->item;
	const char *name = selection->name;
	int len = strlen(event);
	const char *actual;
	const char *field;
	int ret = 0;

	switch (e->type) {
	case EXPR_FIELD:
		if (!selection->name || !e->name)
			break;
		actual = show_raw_expr(e);
		field = event_match(event, actual, len);
		if (field && type != VALUE_TO) {
			print_val_delim(s, start);
			trace_seq_printf(s, "%s=%s", e->name, field);
			ret = add_var(vars, e->name, actual);
		}
		break;
	default:
		if (type == VALUE_TO) {
			print_val_delim(s, start);
			trace_seq_printf(s, "%s=", name);
			print_to_expr(s, table, event, e, vars);
		} else {
			print_from_expr(s, table, event, e, start, vars);
		}
		break;
	}

	return ret;
}

static int print_values(struct trace_seq *s,
			struct sql_table *table, const char *event,
			 enum value_type type, struct var_list **vars)
{
	struct selection *selection;
	struct expression *e;
	char *f, *p;
	bool start = true;
	int ret = 0;

	if (event) {
		f = strdup(event);
		if ((p = strstr(f, ".")))
			*p = '\0';

		for (selection = table->selections; selection; selection = selection->next) {
			ret = print_value(s, table, f, selection, type,
					  &start, vars);
		}
		free(f);
	} else {
		for (selection = table->selections; selection; selection = selection->next) {
			e = selection->item;
			if (e->name && strncmp(e->name, "key", 3) == 0)
				continue;
			if (start) {
				trace_seq_printf(s, ":values=");
				start = false;
			} else {
				trace_seq_printf(s, ",");
			}
			trace_seq_printf(s, "%s", show_raw_expr(e));
		}
	}
	return ret;
}

static void print_trace_field(struct trace_seq *s,
			      struct sql_table *table, struct selection *selection)
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
		trace_seq_printf(s, ",%s", field);
		return;
	}

	name = selection->name;
	if (!name)
		name = e->name;
	if (name) {
		trace_seq_printf(s, ",$%s", name);
		return;
	}

	trace_seq_printf(s, ",ERROR");
}

static void print_trace(struct trace_seq *s, struct sql_table *table)
{
	struct selection *selection;

	trace_seq_printf(s, ".trace(%s", table->name);

	for (selection = table->selections; selection; selection = selection->next)
		print_trace_field(s, table, selection);
	trace_seq_printf(s, ")");
}

static void print_compare(struct trace_seq *s,
			  struct sql_table *table, const char *event)
{
	struct expression *filter = table->filter;
	struct expression *A, *B;
	const char *op;
	const char *actual;
	const char *field;
	int len;

	if (filter->type != EXPR_FILTER) {
		trace_seq_printf(s, "<NOT A FILTER>");
		return;
	}

	A = filter->A;
	B = filter->B;
	op = filter->op;

	len = strlen(event);

	actual = show_raw_expr(A);
	field = event_match(event, actual, len);
	if (!field)
		return;

	trace_seq_printf(s, " if %s %s %s", field, op, show_expr(B));
}

static void print_filter(struct trace_seq *s,
			 struct sql_table *table, const char *event)
{
	if (!table->filter || !event)
		return;

	print_compare(s, table, event);
}

static void print_system_event(struct trace_seq *s,
			       const char *text, char delim)
{
	struct tep_event *event;
	char *name = strdup(text);
	char *tok;

	strtok(name, ".");
	tok = strtok(NULL, ".");
	if (tok) {
		trace_seq_printf(s, "%s%c%s", tok, delim, name);
		goto out;
	}

	event = find_event(tep, name);
	if (!event) {
		trace_seq_printf(s, "(system)%c%s", delim, name);
		goto out;
	}

	trace_seq_printf(s, "%s%c%s", event->system, delim, name);

 out:
	free(name);
}

static void make_histograms(struct trace_seq *s, struct sqlhist *sqlhist,
			    struct sql_table *table)
{
	struct sql_table *save_curr = curr_table;
	struct var_list *vars = NULL;
	const char *from = NULL;
	const char *to;

	if (!table)
		return;

	/* Need to do children and younger siblings first */
	make_histograms(s, sqlhist, find_table(table->from));

	curr_table = table;

	if (table->to)
		from = resolve(table, table->from);

	trace_seq_reset(s);
	trace_seq_printf(s, "hist:keys=");
	print_keys(s, table, from);
	print_values(s, table, from, VALUE_FROM, &vars);
	print_filter(s, table, from);
	trace_seq_terminate(s);

	sqlhist->start_hist = strdup(s->buffer);

	trace_seq_reset(s);
	if (!table->to)
		from = resolve(table, table->from);
	trace_seq_printf(s, "events/");
	print_system_event(s, from, '/');
	trace_seq_printf(s, "/trigger");
	trace_seq_terminate(s);

	sqlhist->start_path = strdup(s->buffer);

	if (!table->to)
		goto out;

	trace_seq_reset(s);
	trace_seq_printf(s, "hist:keys=");
	to = resolve(table, table->to);
	print_keys(s, table, to);
	print_values(s,table, to, VALUE_TO, &vars);
	trace_seq_printf(s, ":onmatch(");
	print_system_event(s, from, '.');
	trace_seq_printf(s, ")");
	print_trace(s, table);
	print_filter(s, table, to);
	trace_seq_terminate(s);

	sqlhist->end_hist = strdup(s->buffer);

	trace_seq_reset(s);
	trace_seq_printf(s, "events/");
	print_system_event(s, to, '/');
	trace_seq_printf(s, "/trigger");
	trace_seq_terminate(s);

	sqlhist->end_path = strdup(s->buffer);

	while (vars) {
		struct var_list *v = vars;
		vars = v->next;
		free(v);
	}

 out:
	curr_table = save_curr;

	make_histograms(s, sqlhist, find_table(table->to));
}

static void dump_tables(void)
{
	struct trace_seq s;
	static int debug;

	if (!debug)
		return;

	trace_seq_init(&s);
	dump_table(&s, curr_table);

	trace_seq_do_printf(&s);
	trace_seq_destroy(&s);
}

static char *buffer;
static size_t buffer_size;
static size_t buffer_idx;
static char *parse_error_str;

void parse_error(int line, int idx, const char *text,
		 const char *fmt, va_list ap)
{
	struct trace_seq s;
	int i;

	if (!buffer)
		return;

	trace_seq_init(&s);
	if (!s.buffer) {
		fprintf(stderr, "Error allocating internal buffer\n");
		return;
	}

	for (i = 0; line && buffer[i]; i++) {
		if (buffer[i] == '\n')
			line--;
	}
	for (; buffer[i] && buffer[i] != '\n'; i++)
		trace_seq_putc(&s, buffer[i]);
	trace_seq_putc(&s, '\n');
	for (i = idx; i > 0; i--)
		trace_seq_putc(&s, ' ');
	trace_seq_puts(&s, "^\n");
	trace_seq_printf(&s, "ERROR: '%s'\n", text);
	trace_seq_vprintf(&s, fmt, ap);

	trace_seq_terminate(&s);

	parse_error_str = strdup(s.buffer);
	trace_seq_destroy(&s);
}

void print_buffer_line(int line, int idx)
{
	int i;

	if (!buffer)
		return;

	for (i = 0; line && buffer[i]; i++) {
		if (buffer[i] == '\n')
			line--;
	}
	for (; buffer[i] && buffer[i] != '\n'; i++)
		printf("%c", buffer[i]);
	printf("\n");
	for (i = idx; i > 0; i--)
		printf(" ");
	printf("^\n");
}

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

const char *sqlhist_start_event(struct sqlhist *sqlhist)
{
	return sqlhist->start_event;
}

const char *sqlhist_end_event(struct sqlhist *sqlhist)
{
	return sqlhist->end_event;
}

const char *sqlhist_synth_event(struct sqlhist *sqlhist)
{
	return sqlhist->synth_event;
}

const char *sqlhist_synth_event_def(struct sqlhist *sqlhist)
{
	return sqlhist->synth_event_def;
}

const char *sqlhist_start_hist(struct sqlhist *sqlhist)
{
	return sqlhist->start_hist;
}

const char *sqlhist_end_hist(struct sqlhist *sqlhist)
{
	return sqlhist->end_hist;
}

const char *sqlhist_start_path(struct sqlhist *sqlhist)
{
	return sqlhist->start_path;
}

const char *sqlhist_end_path(struct sqlhist *sqlhist)
{
	return sqlhist->end_path;
}

const char *sqlhist_synth_filter(struct sqlhist *sqlhist)
{
	return sqlhist->synth_filter;
}

const char *sqlhist_error(struct sqlhist *sqlhist)
{
	return sqlhist->error;
}

struct sqlhist *sqlhist_parse(const char *sql_buffer, const char *trace_dir)
{
	struct sql_table *table;
	struct sqlhist *sqlhist;
	struct trace_seq s;
	int ret;

	if (!sql_buffer)
		return NULL;

	buffer = strdup(sql_buffer);
	if (!buffer)
		return NULL;
	buffer_size = strlen(buffer);

	ret = yyparse();
	free(buffer);

	if (ret == -ENOMEM)
		return NULL;

	dump_tables();

	sqlhist = calloc(1, sizeof(*sqlhist));

	if (!sqlhist)
		return NULL;

	if (ret) {
		sqlhist->error = parse_error_str;
		return sqlhist;
	}

	tep = tracefs_local_events(trace_dir);
	if (!tep) {
		/* Return an empty sqlhist */
		return sqlhist;
	}

	trace_seq_init(&s);
	if (!s.buffer)
		goto fail;

	table = top_table;
	make_synthetic_events(&s, top_table);
	trace_seq_terminate(&s);
	sqlhist->start_event = strdup(resolve(table, table->from));
	if (table->to) {
		sqlhist->end_event = strdup(resolve(table, table->from));
		sqlhist->synth_event = strdup(table->name);
		sqlhist->synth_event_def = strdup(s.buffer);
	}

	trace_seq_reset(&s);
	make_histograms(&s, sqlhist, top_table);
	trace_seq_destroy(&s);

	return sqlhist;

 fail:
	free(sqlhist);
	return NULL;
}

int sqlhist_lex_it(void)
{
	return lex_it();
}
