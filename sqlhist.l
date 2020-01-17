%{
/* code here */

#include <stdarg.h>
#include "sqlhist.h"

extern int my_yyinput(char *buf, int max);

#undef YY_INPUT
#define YY_INPUT(b, r, m) (r = my_yyinput(b, m))

#define YY_NO_INPUT
#define YY_NO_UNPUT

static int line_no = 1;

%}

%option caseless

%%

select { return SELECT; }
as { return AS; }
from { return FROM; }
join { return JOIN; }
on { return ON; }

\$[a-z][a-z0-9_]* { yylval.string = store_str(yytext); return VARIABLE; }

[a-z0-9_\.]+ {  yylval.string = store_str(yytext); return STRING; }

[()\-\+\*/,=] { return yytext[0]; }

[ \t]
\n { line_no++; }

%%

int yywrap(void)
{
	return 1;
}

void yyerror(const char *fmt, ...)
{
	va_list ap;

	printf("\n");
	va_start(ap, fmt);
	printf("ERROR: (%s) at line %d\n", yytext, line_no);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}
