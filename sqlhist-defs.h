#ifndef _SQLHIST_DEFS_H
#define _SQLHIST_DEFS_H

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
	EXPR_FILTER,
};

struct sql_table;

struct expression {
	enum expr_type		type;
	void			*A;
	void			*B;
	const char		*op;
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
	struct expression	*filter;
};

struct sqlhist {
	char			*start_event;
	char			*end_event;
	char			*synth_event;
	char			*synth_event_def;
	char			*start_hist;
	char			*end_hist;
	char			*start_path;
	char			*end_path;
	char			*synth_filter;
	char			*trace_dir;
	char			*error;
};

#endif
