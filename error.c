#include "error.h"

#include <stdio.h>

static int total_errors = 0;
static int semantic_errors = 0;
static int last_lexical_line = -1;
static int last_syntax_line = -1;
static int last_semantic_line = -1;

void error_reset(void) {
    total_errors = 0;
    semantic_errors = 0;
    last_lexical_line = -1;
    last_syntax_line = -1;
    last_semantic_line = -1;
}

void report_lexical_error(int line, const char *message) {
    if(line == last_lexical_line) {
        return;
    }

    printf("Error type A at Line %d: %s.\n", line, message);
    last_lexical_line = line;
    ++total_errors;
}

void report_syntax_error(int line, const char *message) {
    if(line == last_lexical_line) {
        return;
    }
    if(line == last_syntax_line) {
        return;
    }

    printf("Error type B at Line %d: %s.\n", line, message);
    last_syntax_line = line;
    ++total_errors;
}

void report_semantic_error(int type, int line, const char *message) {
    if(line == last_semantic_line) {
        return;
    }

    printf("Error type %d at Line %d: %s.\n", type, line, message);
    last_semantic_line = line;
    ++total_errors;
    ++semantic_errors;
}

int error_count(void) {
    return total_errors;
}

int has_error(void) {
    return total_errors > 0;
}

int has_semantic_error(void) {
    return semantic_errors > 0;
}
