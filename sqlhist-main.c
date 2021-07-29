#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <tracefs.h>

#include "sqlhist.h"

static void __vdie(const char *fmt, va_list ap, int err)
{
	int ret = errno;

	if (err && errno)
		perror("bmp-read");
	else
		ret = -1;

	fprintf(stderr, "  ");
	vfprintf(stderr, fmt, ap);

	fprintf(stderr, "\n");
	exit(ret);
}

void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vdie(fmt, ap, 0);
	va_end(ap);
}

void pdie(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vdie(fmt, ap, 1);
	va_end(ap);
}

static void usage(char **argv)
{
	char *arg = argv[0];
	char *p = arg+strlen(arg);

	while (p >= arg && *p != '/')
		p--;
	p++;

	printf("\nusage: %s [-hl][-t tracefs-path]([-f file]|sql-select-statement)\n"
	       " file : holds sql statement (read from stdin if not present)\n"
	       " -h : show this message\n"
	       " -l : Only run the lexer (for testing)\n"
	       " -t : Path to tracefs directory (looks for it via /proc/mounts if not set)\n"
	       " -f : file to read sql-statement from, instead of command line (use '-' for stdin)\n"
	       "\n",p);
	exit(-1);
}

static int do_parse(const char *buffer, const char *trace_dir)
{
	struct sqlhist *sqlhist;

	sqlhist = sqlhist_parse(buffer, trace_dir);
	if (!sqlhist)
		pdie("Error parsing sqlhist\n");

	if (!sqlhist_start_event(sqlhist))
		die("Error:\n%s", sqlhist_error(sqlhist));

	if (sqlhist_end_event(sqlhist)) {
		printf("echo '%s' > synthetic_events\n",
		       sqlhist_synth_event_def(sqlhist));
	}

	printf("echo '%s' > %s\n",
	       sqlhist_start_hist(sqlhist), sqlhist_start_path(sqlhist));

	if (sqlhist_end_event(sqlhist)) {
		printf("echo '%s' > %s\n",
		       sqlhist_end_hist(sqlhist), sqlhist_end_path(sqlhist));
	}

	sqlhist_destroy(sqlhist);
	return 0;
}

#ifdef HAVE_TRACEFS_SQL
static int do_sql(const char *buffer, const char *trace_dir)
{
	struct tracefs_synth *synth;
	struct tep_handle *tep;
	const char *name = "Anonymous";
	struct trace_seq seq;

	/* Shut up the compiler */
	if (0)
		do_parse(buffer, trace_dir);

	trace_seq_init(&seq);
	tep = tracefs_local_events(trace_dir);
	if (!tep) {
		if (!trace_dir)
			trace_dir = "tracefs directory";
		/* Return an empty sqlhist */
		pdie("Failed to read %s", trace_dir);
	}

	synth = tracefs_sql(tep, name, buffer);
	if (!synth)
		pdie("tracefs_sql");

	tracefs_synth_show(&seq, NULL, synth);
	tracefs_synth_free(synth);

	trace_seq_do_printf(&seq);
	trace_seq_destroy(&seq);
	return 0;
}
#else
static int do_sql(const char *buffer, const char *trace_dir)
{
	return do_parse(buffer, trace_dir);
}
#endif

int main (int argc, char **argv)
{
	char *trace_dir = NULL;
	char *buffer = NULL;
	char buf[BUFSIZ];
	int buffer_size = 0;
	const char *file = NULL;
	FILE *fp;
	size_t r;
	int c;
	int i;

	for (;;) {
		c = getopt(argc, argv, "hlt:f:");
		if (c == -1)
			break;

		switch(c) {
		case 'h':
			usage(argv);
		case 'l':
			return sqlhist_lex_it();
		case 't':
			trace_dir = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		}
	}

	if (file) {
		if (!strcmp(file, "-"))
			fp = stdin;
		else
			fp = fopen(file, "r");
		if (!fp)
			pdie("Error opening: %s", argv[optind]);
		while ((r = fread(buf, 1, BUFSIZ, fp)) > 0) {
			buffer = realloc(buffer, buffer_size + r + 1);
			strncpy(buffer + buffer_size, buf, r);
			buffer_size += r;
		}
		fclose(fp);
		if (buffer_size)
			buffer[buffer_size] = '\0';
	} else if (argc == optind) {
		usage(argv);
	} else {
		for (i = optind; i < argc; i++) {
			r = strlen(argv[i]);
			buffer = realloc(buffer, buffer_size + r + 2);
			if (i != optind)
				buffer[buffer_size++] = ' ';
			strcpy(buffer + buffer_size, argv[i]);
			buffer_size += r;
		}
	}

	do_sql(buffer, trace_dir);
	free(buffer);

	return 0;
}
