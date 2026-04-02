#include "ast.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ast_strdup(const char *source) {
    size_t length;
    char *copy;

    if(source == NULL) {
        return NULL;
    }

    length = strlen(source);
    copy = (char *)malloc(length + 1);
    if(copy == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    memcpy(copy, source, length + 1);
    return copy;
}

static AstNode *ast_new_node(const char *name, int line, AstNodeCategory category) {
    AstNode *node = (AstNode *)calloc(1, sizeof(AstNode));
    if(node == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->name = ast_strdup(name);
    node->line = line;
    node->category = category;
    node->value_kind = AST_VALUE_NONE;
    return node;
}

AstNode *ast_new_terminal(const char *name, int line) {
    return ast_new_node(name, line, AST_TERMINAL);
}

AstNode *ast_new_terminal_string(const char *name, int line, const char *value) {
    AstNode *node = ast_new_terminal(name, line);
    node->value_kind = AST_VALUE_STRING;
    node->value.string_value = ast_strdup(value);
    return node;
}

AstNode *ast_new_terminal_int(const char *name, int line, int value) {
    AstNode *node = ast_new_terminal(name, line);
    node->value_kind = AST_VALUE_INT;
    node->value.int_value = value;
    return node;
}

AstNode *ast_new_terminal_float(const char *name, int line, float value) {
    AstNode *node = ast_new_terminal(name, line);
    node->value_kind = AST_VALUE_FLOAT;
    node->value.float_value = value;
    return node;
}

AstNode *ast_new_empty(void) {
    return ast_new_node("", 0, AST_EMPTY);
}

AstNode *ast_new_nonterminal(const char *name, int child_count, ...) {
    AstNode *node;
    AstNode *head = NULL;
    AstNode *tail = NULL;
    int line = 0;
    int i;
    va_list args;

    va_start(args, child_count);
    for(i = 0; i < child_count; ++i) {
        AstNode *child = va_arg(args, AstNode *);
        if(child == NULL) {
            child = ast_new_empty();
        }
        if(head == NULL) {
            head = child;
        } else {
            tail->next_sibling = child;
        }
        tail = child;
        child->next_sibling = NULL;
        if(line == 0 && child->category != AST_EMPTY) {
            line = child->line;
        }
    }
    va_end(args);

    if(head == NULL) {
        return NULL;
    }

    node = ast_new_node(name, line, AST_NONTERMINAL);
    node->first_child = head;
    return node;
}

int ast_child_count(const AstNode *node) {
    const AstNode *child;
    int count = 0;

    if(node == NULL) {
        return 0;
    }

    for(child = node->first_child; child != NULL; child = child->next_sibling) {
        ++count;
    }
    return count;
}

AstNode *ast_child(const AstNode *node, int index) {
    AstNode *child;
    int i = 0;

    if(node == NULL || index < 0) {
        return NULL;
    }

    child = node->first_child;
    while(child != NULL && i < index) {
        child = child->next_sibling;
        ++i;
    }
    return child;
}

const char *ast_string(const AstNode *node) {
    if(node == NULL || node->value_kind != AST_VALUE_STRING) {
        return NULL;
    }
    return node->value.string_value;
}

int ast_int(const AstNode *node) {
    if(node == NULL || node->value_kind != AST_VALUE_INT) {
        return 0;
    }
    return node->value.int_value;
}

float ast_float(const AstNode *node) {
    if(node == NULL || node->value_kind != AST_VALUE_FLOAT) {
        return 0.0f;
    }
    return node->value.float_value;
}

int ast_is_name(const AstNode *node, const char *name) {
    if(node == NULL || name == NULL) {
        return 0;
    }
    return strcmp(node->name, name) == 0;
}

int ast_is_empty(const AstNode *node) {
    if(node == NULL) {
        return 1;
    }
    return node->category == AST_EMPTY;
}

static void ast_print_indent(int indent) {
    int i;
    for(i = 0; i < indent; ++i) {
        putchar(' ');
    }
}

void ast_print(const AstNode *node, int indent) {
    const AstNode *child;

    if(node == NULL) {
        return;
    }
    if(node->category == AST_EMPTY) {
        return;
    }

    ast_print_indent(indent);
    if(node->category == AST_NONTERMINAL) {
        printf("%s (%d)\n", node->name, node->line);
        for(child = node->first_child; child != NULL; child = child->next_sibling) {
            ast_print(child, indent + 2);
        }
        return;
    }

    if(strcmp(node->name, "ID") == 0 || strcmp(node->name, "TYPE") == 0) {
        printf("%s: %s\n", node->name, node->value.string_value);
    } else if(strcmp(node->name, "INT") == 0) {
        printf("%s: %d\n", node->name, node->value.int_value);
    } else if(strcmp(node->name, "FLOAT") == 0) {
        printf("%s: %f\n", node->name, node->value.float_value);
    } else {
        printf("%s\n", node->name);
    }
}

void ast_free(AstNode *node) {
    AstNode *child;
    AstNode *next;

    if(node == NULL) {
        return;
    }

    child = node->first_child;
    while(child != NULL) {
        next = child->next_sibling;
        ast_free(child);
        child = next;
    }

    free(node->name);
    if(node->value_kind == AST_VALUE_STRING) {
        free(node->value.string_value);
    }
    free(node);
}
