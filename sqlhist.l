%{
/* code here */

#include <stdarg.h>
#include "sqlhist-parse.h"

extern int my_yyinput(char *buf, int max);

#undef YY_INPUT
#define YY_INPUT(b, r, m) ({r = my_yyinput(b, m);})

#define YY_NO_INPUT
#define YY_NO_UNPUT

static int line_no = 0;
static int line_idx = 0;

#define HANDLE_COLUMN do { line_idx += strlen(yytext); } while (0)

%}

%option caseless

%%

select { HANDLE_COLUMN; return SELECT; }
as { HANDLE_COLUMN; return AS; }
from { HANDLE_COLUMN; return FROM; }
join { HANDLE_COLUMN; return JOIN; }
on { HANDLE_COLUMN; return ON; }
where { HANDLE_COLUMN; return WHERE; }

\$[a-z][a-z0-9_]* { HANDLE_COLUMN; yylval.string = store_str(yytext); return VARIABLE; }

[a-z0-9_\.]+ {  HANDLE_COLUMN; yylval.string = store_str(yytext); return STRING; }

\!= { HANDLE_COLUMN; return NEQ; }
\<= { HANDLE_COLUMN; return LE; }
\>= { HANDLE_COLUMN; return GE; }
== { HANDLE_COLUMN; return EQ; }
[<>&~] { HANDLE_COLUMN; return yytext[0]; }

[()\-\+\*/,=] { HANDLE_COLUMN; return yytext[0]; }

[ \t] { HANDLE_COLUMN; }
\n { line_idx = 0; line_no++; }

%%

int yywrap(void)
{
	return 1;
}

void yyerror(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	parse_error(line_no, line_idx - strlen(yytext), yytext, fmt, ap);
	va_end(ap);
}
