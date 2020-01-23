
LD_PATH := -L/usr/local/lib/tracefs/ -L/usr/local/lib/traceevent

test-build-lib = $(if $(shell sh -c 'echo -e "$(1)" | \
	$(CC) $(LD_PATH) -o /dev/null -x c - $(2) &> /dev/null && echo y'), $3)

LIBTRACEFS_AVAILABLE := $(call test-build-lib, \#include <tracefs/tracefs.h>\\nvoid main() {}, -ltracefs, y)

TARGETS = sqlhist

ifneq ($(strip $(LIBTRACEFS_AVAILABLE)), y)
NO_TRACEFS := 1
endif

ifndef NO_TRACEFS
LIBS := $(LD_PATH) -ltracefs -ltraceevent -DHAVE_TRACEFS
target += report_tracefs
else
LIBS :=
target += report_notracefs
endif

all: $(TARGETS)

sqlhist: sqlhist-parse.c sqlhist.tab.c lex.yy.c
	gcc -g -Wall -o $@ $^ $(LIBS)

lex.yy.c: sqlhist.l
	flex $^

sqlhist.tab.c: sqlhist.y
	bison --debug -v -d -o $@ $^

lex: lex.yy.c yywrap.c
	gcc -g -Wall -o $@ $^

clean:
	rm -f lex.yy.c *~ sqlhist.output sqlhist.tab.[ch] sqlhist

PHONY += force
force:

report_tracefs: force
	@echo "tracefs found"
report_notracefs: force
	@echo "No tracefs library found"
