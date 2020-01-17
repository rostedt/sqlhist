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
}

%token AS SELECT FROM JOIN ON WHERE
%token <string> STRING VARIABLE

%left '+' '-'
%left '*' '/'

%type <string> name item selection_expr field label named_field
%type <string> selection_list table_exp selection_item
%type <string> from_clause select_statement event_map
%type <string> join_clause where_clause

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
    select selection_list table_exp { printf("SELECT %s %s\n", $2, $3); }
  ;

selection_list :
   selection_item
 | selection_list ',' selection_item
   				{ $$ = store_printf("%s, %s", $1, $3); }
 ;

selection_item : selection_expr
  ;

selection_expr : 
   selection_expr '+' selection_expr
   				{ $$ = store_printf("%s + %s", $1, $3); }
 | selection_expr '-' selection_expr
   				{ $$ = store_printf("%s - %s", $1, $3); }
 | selection_expr '*' selection_expr
   				{ $$ = store_printf("%s * %s", $1, $3); }
 | selection_expr '/' selection_expr
   				{ $$ = store_printf("%s / %s", $1, $3); }
 | item
 | '(' selection_expr ')' { $$ = store_printf("(%s)", $2); }
 | '(' selection_expr ')' label
			{
				add_label($4, "EXPRE");
				$$ = store_str($4);
			}
 ;

item :
   named_field 
 | field
 ;

field :
   STRING
 | VARIABLE
 ;

named_field :
   field label { $$ = store_str($2); add_label($2, $1); }
 ;

name :
   STRING { printf("name = %s\n", $$); }
 ;

event_map :
   from_clause join_clause on_clause { $$ = store_printf("%s TO %s", $1, $2); }
 ;

where_clause :
   WHERE item { $$ = store_printf(" WHERE %s", $2); }
 ;

table_exp :
   event_map
 | event_map where_clause
 ;

from_clause :
   FROM item			{ $$ = store_printf("FROM %s", $2); }

 | FROM '(' select_statement ')' label
				{
					add_table($5); table_end($5);
					$$ = store_printf("($s) %s", $3, $5);
				}
 | FROM '(' select_statement ')'
				{ table_end(NULL); $$ = store_printf("($s)", $3); }
 ;

join_clause :
  JOIN item	{ $$ = store_str($2); }
 ;

on_clause :
  ON match_clause
 ;

match :
   item '=' item { add_match($1, $3); }
 ;

match_clause :
   match
 | match_clause ',' match
 ;

%%


