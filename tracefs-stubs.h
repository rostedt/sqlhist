#ifndef __TRACEFS_STUBS
#define __TRACEFS_STUBS

#include <stdio.h>
#include <errno.h>

struct tep_handle {
};

struct tep_format_field {
	char		*type;
};

struct tep_event {
	char		*system;
};

static inline struct tep_handle *
 tracefs_local_events(const char *dir)
{
	errno = ENODEV;
	return NULL;
}

static inline struct tep_event *
 tep_find_event_by_name(struct tep_handle *tep, const char *system, const char *event)
{
	return NULL;
}

static inline struct tep_format_field *
 tep_find_any_field(struct tep_event *event, const char *field)
{
	return NULL;
}

#endif


