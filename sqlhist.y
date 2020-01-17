%{
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlhist.h"

extern int yylex(void);
extern void yyerror(char *fmt, ...);

%}

%union {
	int	s32;
	char	*string;
	void	*expr;
}

%token AS SELECT FROM JOIN ON WHERE
%token <string> STRING VARIABLE

%left '+' '-'
%left '*' '/'

%type <string> name field label
%type <string> selection_list table_exp selection_item
%type <string> from_clause select_statement event_map
%type <string> join_clause where_clause

%type <expr>  selection_expr item named_field

%%

start:
   select_statement { table_end(NULL); }
 | select_name
 ;

select_name :
   '(' select_statement ')' label
			{ table_end($4); add_label($4, "SELECT"); }
 ;

label : AS name { $$ = store_printf("%s", $2); }
 | name
 ;

select : SELECT  { table_start(); printf("starting SELECT\n"); }
  ;

select_statement :
    select selection_list table_exp
				{
					$$ = store_printf("SELECT %s %s", $2, $3);
					printf("%s\n", $$);
				}
  ;

selection_list :
   selection_item
 | selection_list ',' selection_item
   				{
					$$ = store_printf("%s, %s", $1, $3);
				}
 ;

selection_item : selection_expr { $$ = show_expr($1); add_selection($1); }
  ;

selection_expr : 
   selection_expr '+' selection_expr
   				{
					$$ = add_plus($1, $3);
				}
 | selection_expr '-' selection_expr
   				{
					$$ = add_minus($1, $3);
				}
 | selection_expr '*' selection_expr
   				{
					$$ = add_mult($1, $3);
				}
 | selection_expr '/' selection_expr
   				{
					$$ = add_divid($1, $3);
				}
 | item
 | '(' selection_expr ')' { $$ = $2; }
 | '(' selection_expr ')' label
			{
				add_expr($4, $2);
				printf("add expr %s\n", show_expr($2));
				$$ = $2;
			}
 ;

item :
   named_field 
 | field		{ $$ = add_field($1, NULL); }
 ;

field :
   STRING
 | VARIABLE
 ;

named_field :
   field label { $$ = add_field($1, $2); }
 ;

name :
   STRING { printf("name = %s\n", $$); }
 ;

event_map :
   from_clause join_clause on_clause { $$ = store_printf("%s TO %s", $1, $2); }
 ;

where_clause :
   WHERE item { $$ = store_printf(" WHERE %s", show_expr($2)); }
 ;

table_exp :
   event_map
 | event_map where_clause
 ;

from_clause :
   FROM item			{ $$ = store_printf("FROM %s", show_expr($2)); }

 | FROM '(' select_statement ')' label
				{
					add_table($5); table_end($5);
					$$ = store_printf("FROM (%s) AS %s", $3, $5);
				}
 | FROM '(' select_statement ')'
				{ table_end(NULL); $$ = store_printf("FROM ($s)", $3); }
 ;

join_clause :
  JOIN item	{ $$ = store_str(show_expr($2)); }
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


