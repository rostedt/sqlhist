
sqlhist: sqlhist-parse.c sqlhist.tab.c lex.yy.c
	gcc -g -Wall -o $@ $^

lex.yy.c: sqlhist.l
	flex $^

sqlhist.tab.c: sqlhist.y
	bison --debug -v -d -o $@ $^

lex: lex.yy.c yywrap.c
	gcc -g -Wall -o $@ $^

clean:
	rm -f lex.yy.c *~ sqlhist.output sqlhist.tab.[ch] sqlhist
