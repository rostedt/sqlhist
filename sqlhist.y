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

%token AS SELECT  FROM
%token <string> STRING VARIABLE

%left '+' '-'
%left '*' '/'

%type <string> name item selection_expr field label named_field
%type <string> selection_list table_exp selection_item
%type <string> from_clause select_statement

%%

start: select_statement
 | select_name
 ;

select_name :
   '(' select_statement ')' label
				{ add_label($4, "SELECT"); }
 ;

label : AS name { $$ = store_printf("%s", $2); }
 | name
 ;

select_statement :
    SELECT selection_list table_exp { printf("SELECT %s %s\n", $2, $3); }
  ;

selection_list :
   selection_item
 | selection_list ',' selection_item
   				{ $$ = store_printf("%s, %s", $1, $3); }
 ;

selection_item : selection_expr ;

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
				$$ = store_printf("(%s) AS %s", $2);
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
   field label { add_label($2, $1); }
 ;

name :
   STRING { printf("name = %s\n", $$); }
 ;

table_exp :
   from_clause 
 ;

from_clause :
   FROM item			{ $$ = store_printf("FROM %s\n", $2); }
 | FROM '(' select_statement ')' label
 				{ $$ = store_printf("($s) %s\n", $3, $5); }
 | FROM '(' select_statement ')'
 				{ $$ = store_printf("($s)\n", $3); }
 ;

%%


