
LD_PATH := $(shell sh -c 'pkg-config --cflags --libs libtracefs')

test-build-lib = $(if $(shell sh -c 'echo -e "$(1)" | \
	$(CC) $(LD_PATH) -o /dev/null -x c - &>/dev/null && echo y'), $2)

#test-build-lib2 = $(if $(shell sh -c 'echo -e "$(1) $(CC) $(LD_PATH) -o /dev/null -x c - && echo y"'), $2)

#LIBTRACEFS_AVAILABLE := $(call test-build-lib, #include <tracefs.h>\\nvoid main() {}, y)

LIBTRACEFS_SQL_AVAILABLE := $(call test-build-lib, #include <tracefs.h>\\nvoid main() { tracefs_sql(NULL, NULL, NULL); }, y)

TARGETS = sqlhist

ifneq ($(strip $(LIBTRACEFS_AVAILABLE)), y)
NO_TRACEFS := 1
endif

PKG_CONFIG = pkg-config
TRACEFS_INCLUDES = $(shell $(PKG_CONFIG) --cflags libtracefs)
TRACEFS_LIBS = $(shell $(PKG_CONFIG) --libs libtracefs)

TRACEEVENT_INCLUDES = $(shell $(PKG_CONFIG) --cflags libtraceevent)
TRACEEVENT_LIBS = $(shell $(PKG_CONFIG) --libs libtraceevent)

ifeq ($(TRACEFS_INCLUDES),'')
TARGETS += report_notracefs
else
 ifeq ($(strip $(LIBTRACEFS_SQL_AVAILABLE)),y)
    SQL_AVAIL = -DHAVE_TRACEFS_SQL
    SQL_REPORT = report_tracefs_sql
 endif
TRACEFS_INCLUDES += -DHAVE_TRACEFS $(SQL_AVAIL)
TARGETS += report_tracefs $(SQL_REPORT)
endif

LIBS = $(TRACEFS_LIBS) $(TRACEEVENT_LIBS) -ldl

CFLAGS := $(TRACEFS_INCLUDES) $(TRACEEVENT_INCLUDES)

all: $(TARGETS)

sqlhist: sqlhist-main.c sqlhist-core.c sqlhist-parse.c sqlhist.tab.c lex.yy.c
	gcc -g -Wall -o $@ $(CFLAGS) $^ $(LIBS)

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

report_tracefs_sql: force
	@echo " and has sql support"
