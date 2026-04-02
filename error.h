#ifndef ERROR_H
#define ERROR_H

void error_reset(void);
void report_lexical_error(int line, const char *message);
void report_syntax_error(int line, const char *message);
void report_semantic_error(int type, int line, const char *message);
int error_count(void);
int has_error(void);
int has_semantic_error(void);

#endif
