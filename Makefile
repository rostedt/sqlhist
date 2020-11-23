
LD_PATH := -L/usr/local/lib/tracefs/ -L/usr/local/lib/traceevent

test-build-lib = $(if $(shell sh -c 'echo -e "$(1)" | \
	$(CC) $(LD_PATH) -o /dev/null -x c - $(2) &> /dev/null && echo y'), $3)

LIBTRACEFS_AVAILABLE := $(call test-build-lib, \#include <tracefs/tracefs.h>\\nvoid main() {}, -ltracefs, y)

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
TRACEFS_INCLUDES += -DHAVE_TRACEFS
TARGETS += report_tracefs
endif

LIBS = $(TRACEFS_LIBS) $(TRACEEVENT_LIBS) -ldl

CFLAGS := $(TRACEFS_INCLUDES) $(TRACEEVENT_INCLUDES)

all: $(TARGETS)

sqlhist: sqlhist-parse.c sqlhist.tab.c lex.yy.c
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
