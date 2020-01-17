#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>

#include "sqlhist.h"

/*
 *  Ideally, we will conevert:
 *
 * trace-cmd start						\
 *    --sql '(select start.common_timestamp as start_time,
 *                    end.common_timestamp as end_time, start.pid,
 *                   (start_time - end_time) as delta
 *            from sched_waking as start
 *            join sched_switch as end
 *              on start.pid = end.next_pid) as first'
 *
 * to this:
 *
 * # echo 'first u64 start_time u64 end_time pid_t pid u64 delta' >> synthetic_events 
 * # echo 'hist:keys=pid:start=common_timestamp' > 
 *                      events/sched/sched_waking/trigger 
 * # echo 'hist:keys=next_pid:start2=$start,delta=common_timestamp-$start:onmatch(sched.sched_waking).trace(first,$start2,common_timestamp,next_pid,$delta)' >
 *                       events/sched/sched_switch/trigger
 */

void die(const char *fmt, ...);

#define HASH_BITS 10

struct str_hash {
	struct str_hash		*next;
	char			*str;
};

static struct str_hash *str_hash[1 << HASH_BITS];

struct label_map {
	struct label_map	*next;
	const char		*label;
	const char		*value;
};

static struct label_map *label_maps;

void add_label(const char *label, const char *val)
{
	struct label_map *lmap;

	lmap = malloc(sizeof(*lmap));
	if (!lmap)
		die("malloc");
	lmap->label = strdup(label);
	lmap->value = strdup(val);

	lmap->next = label_maps;
	label_maps = lmap;
}

static inline unsigned int quick_hash(const char *str)
{
	unsigned int val = 0;
	int len = strlen(str);

	for (; len >= 4; str += 4, len -= 4) {
		val += str[0];
		val += str[1] << 8;
		val += str[2] << 16;
		val += str[3] << 24;
	}
	for (; len > 0; str++, len--)
		val += str[0] << (len * 8);

        val *= 2654435761;

        return val & ((1 << HASH_BITS) - 1);
}


static struct str_hash *find_string(const char *str)
{
	unsigned int key = quick_hash(str);
	struct str_hash *hash;

	for (hash = str_hash[key]; hash; hash = hash->next) {
		if (!strcmp(hash->str, str))
			return hash;
	}
	return NULL;
}

static char **add_hash(const char *str)
{
	struct str_hash *hash;
	unsigned int key;

	if ((hash = find_string(str))) {
		return &hash->str;
	}

	hash = malloc(sizeof(*hash));
	key = quick_hash(str);
	hash->next = str_hash[key];
	str_hash[key] = hash;
	hash->str = NULL;
	return &hash->str;
}

char *store_str(const char *str)
{
	char **pstr = add_hash(str);

	if (!(*pstr))
		*pstr = strdup(str);

	return *pstr;
}

char * store_printf(const char *fmt, ...)
{
	va_list ap;
	char **pstr;
	char *str;
	int ret;

	va_start(ap, fmt);
	ret = vasprintf(&str, fmt, ap);
	va_end(ap);

	if (!ret)
		return NULL; /* ?? */

	pstr = add_hash(str);
	if (*pstr)
		free(str);
	else
		*pstr = str;

	return *pstr;
}

static void usage(char **argv)
{
	char *arg = argv[0];
	char *p = arg+strlen(arg);

	while (p >= arg && *p != '/')
		p--;
	p++;

	printf("usage: %s events\n"
	       "\n",p);
	exit(-1);
}

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

extern int yylex(void);
extern char *yytext;

static int lex_it(void)
{
	int ret;

	do {
		ret = yylex();
	} while (ret > 0);

	return ret;
}

void dump_label_map(void)
{
	struct label_map *lmap;

	if (label_maps)
		printf("Labels:\n");
	for (lmap = label_maps; lmap; lmap = lmap->next) {
		printf("  %s = %s\n", lmap->label, lmap->value);
	}
}

static void parse_it(void)
{
	int ret;

	printf("parsing\n");

	ret = yyparse();
	printf("ret = %d\n", ret);

	dump_label_map();
}

static char *buffer;
static size_t buffer_size;
static size_t buffer_idx;

int my_yyinput(char *buf, int max)
{
	if (!buffer)
		return read(0, buf, max);
	
	if (buffer_idx + max > buffer_size)
		max = buffer_size - buffer_idx;

	if (max)
		memcpy(buf, buffer + buffer_idx, max);

	buffer_idx += max;
	
	return max;
}

int main (int argc, char **argv)
{
	char buf[BUFSIZ];
	FILE *fp;
	size_t r;

	if (argc < 1)
		usage(argv);

	if (argc > 1) {
		if (!strcmp(argv[1],"l"))
			return lex_it();

		fp = fopen(argv[1], "r");
		while ((r = fread(buf, 1, BUFSIZ, fp)) > 0) {
			buffer = realloc(buffer, buffer_size + r + 1);
			strncpy(buffer + buffer_size, buf, r);
			buffer_size += r;
		}
		fclose(fp);
		if (buffer_size)
			buffer[buffer_size] = '\0';
	}

	parse_it();

	return 0;
}
