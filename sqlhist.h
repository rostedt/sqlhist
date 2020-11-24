#ifndef __SQLHIST_H
#define __SQLHIST_H

struct sqlhist;

const char *sqlhist_start_event(struct sqlhist *sqlhist);
const char *sqlhist_end_event(struct sqlhist *sqlhist);
const char *sqlhist_synth_event(struct sqlhist *sqlhist);
const char *sqlhist_synth_event_def(struct sqlhist *sqlhist);
const char *sqlhist_start_hist(struct sqlhist *sqlhist);
const char *sqlhist_end_hist(struct sqlhist *sqlhist);
const char *sqlhist_synth_filter(struct sqlhist *sqlhist);

struct sqlhist *sqlhist_parse(const char *buffer, const char *trace_dir);
int sqlhist_lex_it(void);

#endif
