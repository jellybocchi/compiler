#ifndef AST_H
#define AST_H

typedef enum {
    AST_NONTERMINAL = 0,
    AST_TERMINAL = 1,
    AST_EMPTY = 2
} AstNodeCategory;

typedef enum {
    AST_VALUE_NONE = 0,
    AST_VALUE_STRING = 1,
    AST_VALUE_INT = 2,
    AST_VALUE_FLOAT = 3
} AstValueKind;

typedef struct AstNode {
    char *name;
    int line;
    AstNodeCategory category;
    AstValueKind value_kind;
    union {
        char *string_value;
        int int_value;
        float float_value;
    } value;
    struct AstNode *first_child;
    struct AstNode *next_sibling;
} AstNode;

AstNode *ast_new_terminal(const char *name, int line);
AstNode *ast_new_terminal_string(const char *name, int line, const char *value);
AstNode *ast_new_terminal_int(const char *name, int line, int value);
AstNode *ast_new_terminal_float(const char *name, int line, float value);
AstNode *ast_new_empty(void);
AstNode *ast_new_nonterminal(const char *name, int child_count, ...);

int ast_child_count(const AstNode *node);
AstNode *ast_child(const AstNode *node, int index);
const char *ast_string(const AstNode *node);
int ast_int(const AstNode *node);
float ast_float(const AstNode *node);
int ast_is_name(const AstNode *node, const char *name);
int ast_is_empty(const AstNode *node);

void ast_print(const AstNode *node, int indent);
void ast_free(AstNode *node);

#endif
