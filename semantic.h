#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

typedef struct Type Type;
typedef struct Field Field;
typedef struct Symbol Symbol;
typedef struct FunctionSymbol FunctionSymbol;
typedef struct SemanticContext SemanticContext;

typedef enum {
    TYPE_BASIC = 0,
    TYPE_ARRAY = 1,
    TYPE_STRUCT = 2
} TypeKind;

typedef enum {
    BASIC_INT = 0,
    BASIC_FLOAT = 1
} BasicKind;

struct Field {
    char *name;
    Type *type;
    int line;
    Field *next;
};

struct Type {
    TypeKind kind;
    int size;
    Type *next;
    union {
        BasicKind basic;
        struct {
            Type *element;
            int length;
        } array;
        struct {
            char *name;
            Field *fields;
            int anonymous_id;
        } structure;
    } info;
    Type *alloc_next;
};

struct Symbol {
    char *name;
    Type *type;
    int line;
    char *ir_name;
    Symbol *next;
};

struct FunctionSymbol {
    char *name;
    Type *return_type;
    Field *params;
    int line;
    int defined;
    FunctionSymbol *next;
};

struct SemanticContext {
    Symbol *variables;
    FunctionSymbol *functions;
    Type *structures;
    Type *allocated_types;
    int next_anonymous_struct_id;
    int next_ir_var_id;
};

SemanticContext *semantic_analyze(AstNode *root);
void semantic_context_free(SemanticContext *context);
Symbol *semantic_find_variable(SemanticContext *context, const char *name);
FunctionSymbol *semantic_find_function(SemanticContext *context, const char *name);
Type *semantic_find_structure(SemanticContext *context, const char *name);
int semantic_type_equal(const Type *left, const Type *right);
int semantic_type_size(const Type *type);

#endif
