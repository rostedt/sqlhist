%{
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sqlhist-parse.h"

#define scanner sb->scanner

extern int yylex(YYSTYPE *yylval, void *);
extern void yyerror(struct sqlhist_bison *, char *fmt, ...);

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

%define api.pure
%lex-param {void *scanner}
%parse-param {struct sqlhist_bison *sb}

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
%type <string> from_clause select_statement
%type <string> where_clause compare

%type <expr>  selection_expr item named_field join_clause
%type <expr>  opt_join_clause

%%

start:
   select_statement { table_end(sb, NULL); }
 | select_name
 ;

select_name :
   '(' select_statement ')' label
			{
				CHECK_RETURN_VAL(table_end(sb, $4));
				CHECK_RETURN_VAL(add_label(sb, $4, "SELECT"));
			}
 ;

label : AS name { CHECK_RETURN_PTR($$ = store_printf(sb, "%s", $2)); }
 | name
 ;

select : SELECT  { table_start(sb); }
  ;

select_statement :
    select selection_list table_exp
				{
					$$ = store_printf(sb, "SELECT %s %s", $2, $3);
					CHECK_RETURN_PTR($$);
				}
  ;

selection_list :
   selection_item
 | selection_list ',' selection_item
   				{
					$$ = store_printf(sb, "%s, %s", $1, $3);
					CHECK_RETURN_PTR($$);
				}
 ;

selection_item : selection_expr
				{
					$$ = store_str(sb, show_expr($1));
					CHECK_RETURN_PTR($$);
					CHECK_RETURN_VAL(add_selection(sb, $1));
				}
  ;

selection_expr : 
   selection_expr '+' selection_expr
   				{
					$$ = add_plus(sb, $1, $3);
					CHECK_RETURN_PTR($$);
				}
 | selection_expr '-' selection_expr
   				{
					$$ = add_minus(sb, $1, $3);
					CHECK_RETURN_PTR($$);
				}
 | selection_expr '*' selection_expr
   				{
					$$ = add_mult(sb, $1, $3);
					CHECK_RETURN_PTR($$);
				}
 | selection_expr '/' selection_expr
   				{
					$$ = add_divid(sb, $1, $3);
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
 | field		{ $$ = add_field(sb, $1, NULL); CHECK_RETURN_PTR($$); }
 ;

field :
   STRING
 | VARIABLE
 ;

named_field :
   field label { $$ = add_field(sb, $1, $2); CHECK_RETURN_PTR($$); }
 ;

name :
   STRING
 ;

compare :
   field '<' field	{ $$ = add_filter(sb, $1, $3, "<"); CHECK_RETURN_PTR($$); }
 | field '>' field	{ $$ = add_filter(sb, $1, $3, ">"); CHECK_RETURN_PTR($$); }
 | field LE field	{ $$ = add_filter(sb, $1, $3, "<="); CHECK_RETURN_PTR($$); }
 | field GE field	{ $$ = add_filter(sb, $1, $3, ">="); CHECK_RETURN_PTR($$); }
 | field '=' field	{ $$ = add_filter(sb, $1, $3, "=="); CHECK_RETURN_PTR($$); }
 | field EQ field	{ $$ = add_filter(sb, $1, $3, "=="); CHECK_RETURN_PTR($$); }
 | field NEQ field	{ $$ = add_filter(sb, $1, $3, "!="); CHECK_RETURN_PTR($$); }
 | field '&' field	{ $$ = add_filter(sb, $1, $3, "&"); CHECK_RETURN_PTR($$); }
 | field '~' field	{ $$ = add_filter(sb, $1, $3, "~"); CHECK_RETURN_PTR($$); }
;

where_clause :
   WHERE compare {
	   $$ = store_printf(sb, " WHERE %s", show_expr($2));
	   CHECK_RETURN_PTR($$);
	   add_where($2);
   }
 ;

opt_where_clause :
   /* empty */
 | where_clause
;

table_exp :
   from_clause opt_join_clause opt_where_clause
 ;

from_clause :
   FROM item
				{
					add_from(sb, $2);
					$$ = store_printf(sb, "FROM %s", show_expr($2));
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
  JOIN item ON match_clause	{
					add_to(sb, $2);
					$$ = store_printf(sb, "TO %s",
							  show_expr($2));
				}
 ;

opt_join_clause :
  /* empty */		{ $$ = NULL; }
 | join_clause
;

match :
   item '=' item { add_match(sb, show_expr($1), show_expr($3)); }
 ;

match_clause :
   match
 | match_clause ',' match
 ;

%%


