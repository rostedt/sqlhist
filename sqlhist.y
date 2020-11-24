%{
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sqlhist-parse.h"

extern int yylex(void);
extern void yyerror(char *fmt, ...);
int x;

#define CHECK_RETURN_PTR(x)					\
	do {							\
		if (!(x)) {					\
			printf("FAILED MEMORY: %s\n", #x);	\
			return -ENOMEM;				\
		}						\
	} while (0)

#define CHECK_RETURN_VAL(x)					\
	do {							\
		if ((x) < 0) {					\
			printf("FAILED MEMORY: %s\n", #x);	\
			return -ENOMEM;				\
		}						\
	} while (0)

%}

%union {
	int	s32;
	char	*string;
	void	*expr;
}

%token AS SELECT FROM JOIN ON WHERE
%token <string> STRING VARIABLE
%token <string> LE GE EQ NEQ TILDA

%left '+' '-'
%left '*' '/'
%left '<' '>'

%type <string> name field label
%type <string> selection_list table_exp selection_item
%type <string> from_clause select_statement event_map
%type <string> where_clause compare

%type <expr>  selection_expr item named_field join_clause

%%

start:
   select_statement { table_end(NULL); }
 | select_name
 | simple_select { simple_table_end(); }
 ;

select_name :
   '(' select_statement ')' label
			{
				CHECK_RETURN_VAL(table_end($4));
				CHECK_RETURN_VAL(add_label($4, "SELECT"));
			}
 ;

label : AS name { CHECK_RETURN_PTR($$ = store_printf("%s", $2)); }
 | name
 ;

select : SELECT  { table_start(); }
  ;

simple_select :
     select selection_list from_clause
  |  select selection_list from_clause where_clause
  ;

select_statement :
    select selection_list table_exp
				{
					$$ = store_printf("SELECT %s %s", $2, $3);
					CHECK_RETURN_PTR($$);
				}
  ;

selection_list :
   selection_item
 | selection_list ',' selection_item
   				{
					$$ = store_printf("%s, %s", $1, $3);
					CHECK_RETURN_PTR($$);
				}
 ;

selection_item : selection_expr
				{
					$$ = store_str(show_expr($1));
					CHECK_RETURN_PTR($$);
					CHECK_RETURN_VAL(add_selection($1));
				}
  ;

selection_expr : 
   selection_expr '+' selection_expr
   				{
					$$ = add_plus($1, $3);
					CHECK_RETURN_PTR($$);
				}
 | selection_expr '-' selection_expr
   				{
					$$ = add_minus($1, $3);
					CHECK_RETURN_PTR($$);
				}
 | selection_expr '*' selection_expr
   				{
					$$ = add_mult($1, $3);
					CHECK_RETURN_PTR($$);
				}
 | selection_expr '/' selection_expr
   				{
					$$ = add_divid($1, $3);
					CHECK_RETURN_PTR($$);
				}
 | item
 | '(' selection_expr ')' { $$ = $2; CHECK_RETURN_PTR($$); }
 | '(' selection_expr ')' label
			{
				CHECK_RETURN_VAL(add_expr($4, $2));
				CHECK_RETURN_PTR($$ = $2);
			}
 ;

item :
   named_field 
 | field		{ $$ = add_field($1, NULL); CHECK_RETURN_PTR($$); }
 ;

field :
   STRING
 | VARIABLE
 ;

named_field :
   field label { $$ = add_field($1, $2); CHECK_RETURN_PTR($$); }
 ;

name :
   STRING
 ;

event_map :
   from_clause join_clause on_clause { $$ = store_printf("%s TO %s", $1, show_expr($2)); }
 ;

compare :
   item '<' item	{ $$ = add_filter($1, $3, "<"); CHECK_RETURN_PTR($$); }
 | item '>' item	{ $$ = add_filter($1, $3, ">"); CHECK_RETURN_PTR($$); }
 | item LE item		{ $$ = add_filter($1, $3, "<="); CHECK_RETURN_PTR($$); }
 | item GE item		{ $$ = add_filter($1, $3, ">="); CHECK_RETURN_PTR($$); }
 | item '=' item	{ $$ = add_filter($1, $3, "=="); CHECK_RETURN_PTR($$); }
 | item EQ item		{ $$ = add_filter($1, $3, "=="); CHECK_RETURN_PTR($$); }
 | item NEQ item	{ $$ = add_filter($1, $3, "!="); CHECK_RETURN_PTR($$); }
 | item '&' item	{ $$ = add_filter($1, $3, "&"); CHECK_RETURN_PTR($$); }
 | item '~' item	{ $$ = add_filter($1, $3, "~"); CHECK_RETURN_PTR($$); }
;

where_clause :
   WHERE compare {
	   $$ = store_printf(" WHERE %s", show_expr($2));
	   CHECK_RETURN_PTR($$);
	   add_where($2);
   }
 ;

table_exp :
   event_map
 | event_map where_clause
 ;

from_clause :
   FROM item
				{
					add_from($2);
					$$ = store_printf("FROM %s", show_expr($2));
					CHECK_RETURN_PTR($$);
				}
/*
 * Select from a from clause confuses the variable parsing.
 * disable it for now.

   | FROM '(' select_statement ')' label
				{
					from_table_end($5);
					$$ = store_printf("FROM (%s) AS %s", $3, $5);
				}
*/
 ;

join_clause :
  JOIN item	{ CHECK_RETURN_PTR($2); add_to($2); $$ = $2; }
 ;

on_clause :
  ON match_clause
 ;

match :
   item '=' item { add_match(show_expr($1), show_expr($3)); }
 ;

match_clause :
   match
 | match_clause ',' match
 ;

%%


