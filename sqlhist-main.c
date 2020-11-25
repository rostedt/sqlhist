#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

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

	printf("\nusage: %s [-hl][-t tracefs-path][file]\n"
	       " file : holds sql statement (read from stdin if not present)\n"
	       " -h : show this message\n"
	       " -l : Only run the lexer (for testing)\n"
	       " -t : Path to tracefs directory (looks for it via /proc/mounts if not set)\n"
	       "\n",p);
	exit(-1);
}

int main (int argc, char **argv)
{
	struct sqlhist *sqlhist;
	char *trace_dir = NULL;
	char *buffer = NULL;
	char buf[BUFSIZ];
	int buffer_size = 0;
	FILE *fp;
	size_t r;
	int c;

	for (;;) {
		c = getopt(argc, argv, "hlt:");
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
		}
	}
	if (argc - optind > 0) {

		fp = fopen(argv[optind], "r");
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
	}

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

	return 0;
}
