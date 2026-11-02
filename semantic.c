#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"

typedef struct {
    Type *type;
    int is_lvalue;
    int has_error;
    Symbol *symbol;
} ExpInfo;

static Type basic_int_type = {
    TYPE_BASIC,
    4,
    NULL,
    { .basic = BASIC_INT },
    NULL
};

static Type basic_float_type = {
    TYPE_BASIC,
    4,
    NULL,
    { .basic = BASIC_FLOAT },
    NULL
};

static char *semantic_strdup(const char *source) {
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

static Type *type_new_array(
    SemanticContext *context,
    Type *element,
    int length
) {
    Type *type;

    type = (Type *)calloc(1, sizeof(Type));
    if(type == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    type->kind = TYPE_ARRAY;
    type->info.array.element = element;
    type->info.array.length = length;
    type->size = semantic_type_size(element) * length;
    type->alloc_next = context->allocated_types;
    context->allocated_types = type;
    return type;
}

static Type *type_new_structure(
    SemanticContext *context,
    const char *name,
    Field *fields
) {
    Type *type;
    Field *field;

    type = (Type *)calloc(1, sizeof(Type));
    if(type == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    type->kind = TYPE_STRUCT;
    type->info.structure.fields = fields;
    type->info.structure.anonymous_id = context->next_anonymous_struct_id;
    ++context->next_anonymous_struct_id;

    if(name != NULL) {
        type->info.structure.name = semantic_strdup(name);
    } else {
        char buffer[64];

        snprintf(
            buffer,
            sizeof(buffer),
            "$anon%d",
            type->info.structure.anonymous_id
        );
        type->info.structure.name = semantic_strdup(buffer);
    }

    type->size = 0;
    for(field = fields; field != NULL; field = field->next) {
        type->size += semantic_type_size(field->type);
    }
    type->alloc_next = context->allocated_types;
    context->allocated_types = type;
    return type;
}

static Field *field_new(const char *name, Type *type, int line) {
    Field *field;

    field = (Field *)calloc(1, sizeof(Field));
    if(field == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    field->name = semantic_strdup(name);
    field->type = type;
    field->line = line;
    return field;
}

static Field *field_append(Field *head, Field *tail) {
    Field *current;

    if(head == NULL) {
        return tail;
    }

    current = head;
    while(current->next != NULL) {
        current = current->next;
    }
    current->next = tail;
    return head;
}

static Field *field_find(Field *fields, const char *name) {
    Field *field;

    for(field = fields; field != NULL; field = field->next) {
        if(strcmp(field->name, name) == 0) {
            return field;
        }
    }
    return NULL;
}

static int type_is_int(const Type *type) {
    if(type == NULL) {
        return 0;
    }
    if(type->kind != TYPE_BASIC) {
        return 0;
    }
    return type->info.basic == BASIC_INT;
}

static int type_is_numeric(const Type *type) {
    if(type == NULL) {
        return 0;
    }
    return type->kind == TYPE_BASIC;
}

static int has_name_conflict(SemanticContext *context, const char *name) {
    if(semantic_find_variable(context, name) != NULL) {
        return 1;
    }
    if(semantic_find_function(context, name) != NULL) {
        return 1;
    }
    if(semantic_find_structure(context, name) != NULL) {
        return 1;
    }
    return 0;
}

static void add_builtin_read(SemanticContext *context) {
    FunctionSymbol *function;

    function = (FunctionSymbol *)calloc(1, sizeof(FunctionSymbol));
    if(function == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    function->name = semantic_strdup("read");
    function->return_type = &basic_int_type;
    function->defined = 1;
    function->next = context->functions;
    context->functions = function;
}

static void add_builtin_write(SemanticContext *context) {
    FunctionSymbol *function;

    function = (FunctionSymbol *)calloc(1, sizeof(FunctionSymbol));
    if(function == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    function->name = semantic_strdup("write");
    function->return_type = &basic_int_type;
    function->params = field_new("x", &basic_int_type, 0);
    function->defined = 1;
    function->next = context->functions;
    context->functions = function;
}

Symbol *semantic_find_variable(SemanticContext *context, const char *name) {
    Symbol *symbol;

    if(context == NULL || name == NULL) {
        return NULL;
    }

    for(symbol = context->variables; symbol != NULL; symbol = symbol->next) {
        if(strcmp(symbol->name, name) == 0) {
            return symbol;
        }
    }
    return NULL;
}

FunctionSymbol *semantic_find_function(
    SemanticContext *context,
    const char *name
) {
    FunctionSymbol *function;

    if(context == NULL || name == NULL) {
        return NULL;
    }

    for(function = context->functions; function != NULL; function = function->next) {
        if(strcmp(function->name, name) == 0) {
            return function;
        }
    }
    return NULL;
}

Type *semantic_find_structure(SemanticContext *context, const char *name) {
    Type *type;

    if(context == NULL || name == NULL) {
        return NULL;
    }

    for(type = context->structures; type != NULL; type = type->next) {
        if(
            type->kind == TYPE_STRUCT &&
            strcmp(type->info.structure.name, name) == 0
        ) {
            return type;
        }
    }
    return NULL;
}

int semantic_type_size(const Type *type) {
    if(type == NULL) {
        return 0;
    }
    return type->size;
}

int semantic_type_equal(const Type *left, const Type *right) {
    if(left == right) {
        return 1;
    }
    if(left == NULL || right == NULL) {
        return 0;
    }
    if(left->kind != right->kind) {
        return 0;
    }

    if(left->kind == TYPE_BASIC) {
        return left->info.basic == right->info.basic;
    }
    if(left->kind == TYPE_STRUCT) {
        return strcmp(
            left->info.structure.name,
            right->info.structure.name
        ) == 0;
    }

    return semantic_type_equal(
        left->info.array.element,
        right->info.array.element
    );
}

static void report_named_error(
    int type,
    int line,
    const char *prefix,
    const char *name
) {
    char message[256];

    snprintf(message, sizeof(message), "%s \"%s\"", prefix, name);
    report_semantic_error(type, line, message);
}

static void collect_var_dec(
    AstNode *var_dec,
    const char **name,
    int *line,
    int *dimensions,
    int *dimension_count
) {
    if(ast_child_count(var_dec) == 1) {
        AstNode *id_node;

        id_node = ast_child(var_dec, 0);
        *name = ast_string(id_node);
        *line = id_node->line;
        return;
    }

    collect_var_dec(
        ast_child(var_dec, 0),
        name,
        line,
        dimensions,
        dimension_count
    );
    dimensions[*dimension_count] = ast_int(ast_child(var_dec, 2));
    ++(*dimension_count);
}

static Type *build_var_type(
    SemanticContext *context,
    Type *base_type,
    AstNode *var_dec,
    const char **name,
    int *line
) {
    int dimensions[32];
    int dimension_count;
    int i;
    Type *type;

    dimension_count = 0;
    collect_var_dec(var_dec, name, line, dimensions, &dimension_count);

    type = base_type;
    for(i = dimension_count - 1; i >= 0; --i) {
        type = type_new_array(context, type, dimensions[i]);
    }
    return type;
}

static Symbol *declare_variable(
    SemanticContext *context,
    const char *name,
    Type *type,
    int line
) {
    Symbol *symbol;
    char ir_name[64];

    if(has_name_conflict(context, name)) {
        report_named_error(3, line, "Redefined variable", name);
        return NULL;
    }

    symbol = (Symbol *)calloc(1, sizeof(Symbol));
    if(symbol == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    symbol->name = semantic_strdup(name);
    symbol->type = type;
    symbol->line = line;
    snprintf(ir_name, sizeof(ir_name), "v%d", context->next_ir_var_id);
    ++context->next_ir_var_id;
    symbol->ir_name = semantic_strdup(ir_name);
    symbol->next = context->variables;
    context->variables = symbol;
    return symbol;
}

static void declare_structure(
    SemanticContext *context,
    Type *type,
    const char *name,
    int line
) {
    if(name != NULL && has_name_conflict(context, name)) {
        report_named_error(16, line, "Duplicated name", name);
        return;
    }

    type->next = context->structures;
    context->structures = type;
}

static FunctionSymbol *declare_function(
    SemanticContext *context,
    const char *name,
    Type *return_type,
    Field *params,
    int line
) {
    FunctionSymbol *function;

    if(semantic_find_function(context, name) != NULL) {
        report_named_error(4, line, "Redefined function", name);
        return NULL;
    }
    if(
        semantic_find_variable(context, name) != NULL ||
        semantic_find_structure(context, name) != NULL
    ) {
        report_named_error(4, line, "Redefined function", name);
        return NULL;
    }

    function = (FunctionSymbol *)calloc(1, sizeof(FunctionSymbol));
    if(function == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    function->name = semantic_strdup(name);
    function->return_type = return_type;
    function->params = params;
    function->line = line;
    function->defined = 1;
    function->next = context->functions;
    context->functions = function;
    return function;
}

static Type *analyze_specifier(
    SemanticContext *context,
    AstNode *specifier
);

static Field *analyze_dec_list(
    SemanticContext *context,
    AstNode *dec_list,
    Type *specifier_type,
    int in_structure
);

static void check_struct_fields(Field *fields);

static ExpInfo analyze_exp(
    SemanticContext *context,
    AstNode *exp
);

static void analyze_stmt(
    SemanticContext *context,
    AstNode *stmt,
    Type *return_type
);

static void analyze_stmt_list(
    SemanticContext *context,
    AstNode *stmt_list,
    Type *return_type
);

static void analyze_def_list(
    SemanticContext *context,
    AstNode *def_list,
    int in_structure
);

static Field *analyze_var_list(SemanticContext *context, AstNode *var_list) {
    Field *head;
    AstNode *param_dec;
    AstNode *next_var_list;
    AstNode *specifier;
    AstNode *var_dec;
    const char *name;
    int line;
    Type *type;

    if(var_list == NULL) {
        return NULL;
    }

    head = NULL;
    param_dec = ast_child(var_list, 0);
    specifier = ast_child(param_dec, 0);
    var_dec = ast_child(param_dec, 1);
    name = NULL;
    line = param_dec->line;
    type = build_var_type(context, analyze_specifier(context, specifier), var_dec, &name, &line);
    if(type != NULL && name != NULL) {
        declare_variable(context, name, type, line);
        head = field_new(name, type, line);
    }

    if(ast_child_count(var_list) == 3) {
        next_var_list = ast_child(var_list, 2);
        head = field_append(head, analyze_var_list(context, next_var_list));
    }
    return head;
}

static Field *analyze_struct_def_list(
    SemanticContext *context,
    AstNode *def_list
) {
    Field *result;
    AstNode *def;
    AstNode *next_def_list;
    AstNode *specifier;
    AstNode *dec_list;
    Type *field_type;
    Field *fields;

    if(def_list == NULL) {
        return NULL;
    }

    result = NULL;
    def = ast_child(def_list, 0);
    specifier = ast_child(def, 0);
    dec_list = ast_child(def, 1);
    field_type = analyze_specifier(context, specifier);
    fields = analyze_dec_list(context, dec_list, field_type, 1);
    result = field_append(result, fields);

    next_def_list = ast_child(def_list, 1);
    result = field_append(result, analyze_struct_def_list(context, next_def_list));
    return result;
}

static Type *analyze_struct_specifier(
    SemanticContext *context,
    AstNode *struct_specifier
) {
    int child_count;

    child_count = ast_child_count(struct_specifier);
    if(child_count == 2) {
        AstNode *tag_node;
        const char *name;
        Type *type;

        tag_node = ast_child(ast_child(struct_specifier, 1), 0);
        name = ast_string(tag_node);
        type = semantic_find_structure(context, name);
        if(type == NULL) {
            report_named_error(17, tag_node->line, "Undefined structure", name);
        }
        return type;
    }

    {
        AstNode *opt_tag;
        AstNode *id_node;
        AstNode *def_list;
        const char *name;
        Type *type;
        Field *fields;

        opt_tag = ast_child(struct_specifier, 1);
        id_node = ast_child(opt_tag, 0);
        name = NULL;
        if(id_node != NULL) {
            name = ast_string(id_node);
        }
        def_list = ast_child(struct_specifier, 3);
        fields = analyze_struct_def_list(context, def_list);
        check_struct_fields(fields);
        type = type_new_structure(context, name, fields);
        declare_structure(context, type, name, struct_specifier->line);
        return type;
    }
}

static Type *analyze_specifier(
    SemanticContext *context,
    AstNode *specifier
) {
    AstNode *child;

    if(specifier == NULL) {
        return NULL;
    }

    child = ast_child(specifier, 0);
    if(ast_is_name(child, "TYPE")) {
        if(strcmp(ast_string(child), "int") == 0) {
            return &basic_int_type;
        }
        return &basic_float_type;
    }
    return analyze_struct_specifier(context, child);
}

static Field *analyze_dec_list(
    SemanticContext *context,
    AstNode *dec_list,
    Type *specifier_type,
    int in_structure
) {
    Field *head;
    AstNode *dec;
    AstNode *var_dec;
    AstNode *exp;
    const char *name;
    int line;
    Type *type;
    Field *field;
    ExpInfo exp_info;

    if(dec_list == NULL || specifier_type == NULL) {
        return NULL;
    }

    head = NULL;
    dec = ast_child(dec_list, 0);
    name = NULL;
    line = dec->line;

    if(ast_child_count(dec) == 1) {
        var_dec = ast_child(dec, 0);
        type = build_var_type(context, specifier_type, var_dec, &name, &line);
        if(name != NULL) {
            if(in_structure) {
                field = field_new(name, type, line);
                head = field;
            } else {
                declare_variable(context, name, type, line);
            }
        }
    } else {
        var_dec = ast_child(dec, 0);
        exp = ast_child(dec, 2);
        type = build_var_type(context, specifier_type, var_dec, &name, &line);
        if(in_structure) {
            report_named_error(15, dec->line, "Initialized field", name);
            if(name != NULL) {
                head = field_new(name, type, line);
            }
        } else {
            declare_variable(context, name, type, line);
            exp_info = analyze_exp(context, exp);
            if(
                !exp_info.has_error &&
                !semantic_type_equal(type, exp_info.type)
            ) {
                report_semantic_error(
                    5,
                    dec->line,
                    "Type mismatched for assignment"
                );
            }
        }
    }

    if(ast_child_count(dec_list) == 3) {
        head = field_append(
            head,
            analyze_dec_list(
                context,
                ast_child(dec_list, 2),
                specifier_type,
                in_structure
            )
        );
    }
    return head;
}

static void check_struct_fields(Field *fields) {
    Field *left;
    Field *right;

    for(left = fields; left != NULL; left = left->next) {
        for(right = left->next; right != NULL; right = right->next) {
            if(strcmp(left->name, right->name) == 0) {
                report_named_error(15, right->line, "Redefined field", right->name);
            }
        }
    }
}

static void analyze_def(
    SemanticContext *context,
    AstNode *def,
    int in_structure
) {
    Type *specifier_type;
    Field *fields;

    if(def == NULL) {
        return;
    }

    specifier_type = analyze_specifier(context, ast_child(def, 0));
    fields = analyze_dec_list(
        context,
        ast_child(def, 1),
        specifier_type,
        in_structure
    );
    if(in_structure) {
        check_struct_fields(fields);
    }
}

static void analyze_def_list(
    SemanticContext *context,
    AstNode *def_list,
    int in_structure
) {
    if(def_list == NULL) {
        return;
    }

    analyze_def(context, ast_child(def_list, 0), in_structure);
    analyze_def_list(context, ast_child(def_list, 1), in_structure);
}

static ExpInfo exp_error(void) {
    ExpInfo info;

    memset(&info, 0, sizeof(info));
    info.has_error = 1;
    return info;
}

static ExpInfo exp_ok(Type *type, int is_lvalue, Symbol *symbol) {
    ExpInfo info;

    memset(&info, 0, sizeof(info));
    info.type = type;
    info.is_lvalue = is_lvalue;
    info.symbol = symbol;
    return info;
}

static int args_match(
    SemanticContext *context,
    AstNode *args,
    Field *params
) {
    ExpInfo exp_info;

    if(args == NULL && params == NULL) {
        return 1;
    }
    if(args == NULL || params == NULL) {
        return 0;
    }

    exp_info = analyze_exp(context, ast_child(args, 0));
    if(exp_info.has_error) {
        return 1;
    }
    if(!semantic_type_equal(exp_info.type, params->type)) {
        return 0;
    }

    if(ast_child_count(args) == 1) {
        if(params->next != NULL) {
            return 0;
        }
        return 1;
    }

    return args_match(context, ast_child(args, 2), params->next);
}

static ExpInfo analyze_binary_arithmetic(
    SemanticContext *context,
    AstNode *exp
) {
    ExpInfo left_info;
    ExpInfo right_info;

    left_info = analyze_exp(context, ast_child(exp, 0));
    right_info = analyze_exp(context, ast_child(exp, 2));
    if(left_info.has_error || right_info.has_error) {
        return exp_error();
    }

    if(
        !type_is_numeric(left_info.type) ||
        !type_is_numeric(right_info.type) ||
        !semantic_type_equal(left_info.type, right_info.type)
    ) {
        report_semantic_error(7, exp->line, "Type mismatched for operands");
        return exp_error();
    }

    return exp_ok(left_info.type, 0, NULL);
}

static ExpInfo analyze_exp(
    SemanticContext *context,
    AstNode *exp
) {
    int child_count;
    AstNode *first;

    if(exp == NULL) {
        return exp_error();
    }

    child_count = ast_child_count(exp);
    first = ast_child(exp, 0);

    if(child_count == 1) {
        if(ast_is_name(first, "ID")) {
            Symbol *symbol;

            symbol = semantic_find_variable(context, ast_string(first));
            if(symbol == NULL) {
                report_named_error(1, first->line, "Undefined variable", ast_string(first));
                return exp_error();
            }
            return exp_ok(symbol->type, 1, symbol);
        }
        if(ast_is_name(first, "INT")) {
            return exp_ok(&basic_int_type, 0, NULL);
        }
        if(ast_is_name(first, "FLOAT")) {
            return exp_ok(&basic_float_type, 0, NULL);
        }
    }

    if(child_count == 2) {
        ExpInfo info;

        info = analyze_exp(context, ast_child(exp, 1));
        if(info.has_error) {
            return info;
        }

        if(ast_is_name(first, "MINUS")) {
            if(!type_is_numeric(info.type)) {
                report_semantic_error(7, exp->line, "Type mismatched for operands");
                return exp_error();
            }
            return exp_ok(info.type, 0, NULL);
        }

        if(!type_is_int(info.type)) {
            report_semantic_error(7, exp->line, "Type mismatched for operands");
            return exp_error();
        }
        return exp_ok(&basic_int_type, 0, NULL);
    }

    if(child_count == 3 && ast_is_name(first, "LP")) {
        ExpInfo info;

        info = analyze_exp(context, ast_child(exp, 1));
        if(info.has_error) {
            return info;
        }
        return exp_ok(info.type, info.is_lvalue, info.symbol);
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "ASSIGNOP")) {
        ExpInfo left_info;
        ExpInfo right_info;

        left_info = analyze_exp(context, ast_child(exp, 0));
        right_info = analyze_exp(context, ast_child(exp, 2));
        if(left_info.has_error || right_info.has_error) {
            return exp_error();
        }
        if(!left_info.is_lvalue) {
            report_semantic_error(
                6,
                exp->line,
                "The left-hand side of an assignment must be a variable"
            );
            return exp_error();
        }
        if(!semantic_type_equal(left_info.type, right_info.type)) {
            report_semantic_error(
                5,
                exp->line,
                "Type mismatched for assignment"
            );
            return exp_error();
        }
        return exp_ok(left_info.type, 0, NULL);
    }

    if(
        child_count == 3 &&
        (
            ast_is_name(ast_child(exp, 1), "PLUS") ||
            ast_is_name(ast_child(exp, 1), "MINUS") ||
            ast_is_name(ast_child(exp, 1), "STAR") ||
            ast_is_name(ast_child(exp, 1), "DIV")
        )
    ) {
        return analyze_binary_arithmetic(context, exp);
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "RELOP")) {
        ExpInfo left_info;
        ExpInfo right_info;

        left_info = analyze_exp(context, ast_child(exp, 0));
        right_info = analyze_exp(context, ast_child(exp, 2));
        if(left_info.has_error || right_info.has_error) {
            return exp_error();
        }

        if(
            !type_is_numeric(left_info.type) ||
            !type_is_numeric(right_info.type) ||
            !semantic_type_equal(left_info.type, right_info.type)
        ) {
            report_semantic_error(7, exp->line, "Type mismatched for operands");
            return exp_error();
        }
        return exp_ok(&basic_int_type, 0, NULL);
    }

    if(
        child_count == 3 &&
        (
            ast_is_name(ast_child(exp, 1), "AND") ||
            ast_is_name(ast_child(exp, 1), "OR")
        )
    ) {
        ExpInfo left_info;
        ExpInfo right_info;

        left_info = analyze_exp(context, ast_child(exp, 0));
        right_info = analyze_exp(context, ast_child(exp, 2));
        if(left_info.has_error || right_info.has_error) {
            return exp_error();
        }

        if(!type_is_int(left_info.type) || !type_is_int(right_info.type)) {
            report_semantic_error(7, exp->line, "Type mismatched for operands");
            return exp_error();
        }
        return exp_ok(&basic_int_type, 0, NULL);
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "DOT")) {
        ExpInfo base_info;
        AstNode *field_node;
        Field *field;

        base_info = analyze_exp(context, ast_child(exp, 0));
        if(base_info.has_error) {
            return exp_error();
        }
        if(base_info.type == NULL || base_info.type->kind != TYPE_STRUCT) {
            report_semantic_error(13, exp->line, "Illegal use of \".\"");
            return exp_error();
        }

        field_node = ast_child(exp, 2);
        field = field_find(
            base_info.type->info.structure.fields,
            ast_string(field_node)
        );
        if(field == NULL) {
            report_named_error(14, field_node->line, "Non-existent field", ast_string(field_node));
            return exp_error();
        }
        return exp_ok(field->type, 1, NULL);
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "LP")) {
        AstNode *id_node;
        const char *name;
        FunctionSymbol *function;

        id_node = ast_child(exp, 0);
        name = ast_string(id_node);
        function = semantic_find_function(context, name);
        if(function == NULL) {
            if(semantic_find_variable(context, name) != NULL) {
                report_named_error(11, id_node->line, "Not a function", name);
            } else {
                report_named_error(2, id_node->line, "Undefined function", name);
            }
            return exp_error();
        }
        if(function->params != NULL) {
            report_named_error(9, exp->line, "Function arguments mismatch for", name);
            return exp_error();
        }
        return exp_ok(function->return_type, 0, NULL);
    }

    if(child_count == 4 && ast_is_name(ast_child(exp, 1), "LP")) {
        AstNode *id_node;
        const char *name;
        FunctionSymbol *function;

        id_node = ast_child(exp, 0);
        name = ast_string(id_node);
        function = semantic_find_function(context, name);
        if(function == NULL) {
            if(semantic_find_variable(context, name) != NULL) {
                report_named_error(11, id_node->line, "Not a function", name);
            } else {
                report_named_error(2, id_node->line, "Undefined function", name);
            }
            return exp_error();
        }
        if(!args_match(context, ast_child(exp, 2), function->params)) {
            report_named_error(9, exp->line, "Function arguments mismatch for", name);
            return exp_error();
        }
        return exp_ok(function->return_type, 0, NULL);
    }

    if(child_count == 4 && ast_is_name(ast_child(exp, 1), "LB")) {
        ExpInfo array_info;
        ExpInfo index_info;

        array_info = analyze_exp(context, ast_child(exp, 0));
        index_info = analyze_exp(context, ast_child(exp, 2));
        if(array_info.has_error || index_info.has_error) {
            return exp_error();
        }
        if(array_info.type == NULL || array_info.type->kind != TYPE_ARRAY) {
            if(
                array_info.symbol != NULL &&
                array_info.symbol->name != NULL
            ) {
                report_named_error(
                    10,
                    exp->line,
                    "Not an array",
                    array_info.symbol->name
                );
            } else {
                report_semantic_error(10, exp->line, "Not an array");
            }
            return exp_error();
        }
        if(!type_is_int(index_info.type)) {
            report_semantic_error(12, exp->line, "Array index is not an integer");
            return exp_error();
        }
        return exp_ok(array_info.type->info.array.element, 1, NULL);
    }

    return exp_error();
}

static void analyze_compst(
    SemanticContext *context,
    AstNode *comp_st,
    Type *return_type
) {
    if(comp_st == NULL) {
        return;
    }

    analyze_def_list(context, ast_child(comp_st, 1), 0);
    analyze_stmt_list(context, ast_child(comp_st, 2), return_type);
}

static void analyze_stmt(
    SemanticContext *context,
    AstNode *stmt,
    Type *return_type
) {
    int child_count;
    AstNode *first;
    ExpInfo info;

    if(stmt == NULL) {
        return;
    }

    child_count = ast_child_count(stmt);
    first = ast_child(stmt, 0);

    if(child_count == 1) {
        analyze_compst(context, first, return_type);
        return;
    }

    if(child_count == 2) {
        analyze_exp(context, first);
        return;
    }

    if(ast_is_name(first, "RETURN")) {
        info = analyze_exp(context, ast_child(stmt, 1));
        if(!info.has_error && !semantic_type_equal(info.type, return_type)) {
            report_semantic_error(8, stmt->line, "Type mismatched for return");
        }
        return;
    }

    if(ast_is_name(first, "IF")) {
        info = analyze_exp(context, ast_child(stmt, 2));
        if(!info.has_error && !type_is_int(info.type)) {
            report_semantic_error(7, stmt->line, "Type mismatched for operands");
        }
        analyze_stmt(context, ast_child(stmt, 4), return_type);
        if(child_count == 7) {
            analyze_stmt(context, ast_child(stmt, 6), return_type);
        }
        return;
    }

    if(ast_is_name(first, "WHILE")) {
        info = analyze_exp(context, ast_child(stmt, 2));
        if(!info.has_error && !type_is_int(info.type)) {
            report_semantic_error(7, stmt->line, "Type mismatched for operands");
        }
        analyze_stmt(context, ast_child(stmt, 4), return_type);
    }
}

static void analyze_stmt_list(
    SemanticContext *context,
    AstNode *stmt_list,
    Type *return_type
) {
    if(stmt_list == NULL) {
        return;
    }

    analyze_stmt(context, ast_child(stmt_list, 0), return_type);
    analyze_stmt_list(context, ast_child(stmt_list, 1), return_type);
}

static void analyze_ext_dec_list(
    SemanticContext *context,
    AstNode *ext_dec_list,
    Type *specifier_type
) {
    AstNode *var_dec;
    const char *name;
    int line;
    Type *type;

    if(ext_dec_list == NULL || specifier_type == NULL) {
        return;
    }

    name = NULL;
    line = ext_dec_list->line;
    var_dec = ast_child(ext_dec_list, 0);
    type = build_var_type(context, specifier_type, var_dec, &name, &line);
    if(name != NULL) {
        declare_variable(context, name, type, line);
    }

    if(ast_child_count(ext_dec_list) == 3) {
        analyze_ext_dec_list(context, ast_child(ext_dec_list, 2), specifier_type);
    }
}

static void analyze_function(
    SemanticContext *context,
    AstNode *fun_dec,
    Type *return_type,
    AstNode *comp_st
) {
    AstNode *id_node;
    AstNode *var_list;
    Field *params;
    FunctionSymbol *function;

    id_node = ast_child(fun_dec, 0);
    params = NULL;
    if(ast_child_count(fun_dec) == 4) {
        var_list = ast_child(fun_dec, 2);
        params = analyze_var_list(context, var_list);
    }

    function = declare_function(
        context,
        ast_string(id_node),
        return_type,
        params,
        id_node->line
    );
    if(function == NULL) {
        return;
    }

    analyze_compst(context, comp_st, return_type);
}

static void analyze_ext_def(SemanticContext *context, AstNode *ext_def) {
    int child_count;
    Type *specifier_type;

    if(ext_def == NULL) {
        return;
    }

    child_count = ast_child_count(ext_def);
    specifier_type = analyze_specifier(context, ast_child(ext_def, 0));

    if(child_count == 2) {
        return;
    }
    if(child_count == 3 && ast_is_name(ast_child(ext_def, 1), "ExtDecList")) {
        analyze_ext_dec_list(context, ast_child(ext_def, 1), specifier_type);
        return;
    }
    if(child_count == 3) {
        analyze_function(
            context,
            ast_child(ext_def, 1),
            specifier_type,
            ast_child(ext_def, 2)
        );
    }
}

static void analyze_ext_def_list(
    SemanticContext *context,
    AstNode *ext_def_list
) {
    if(ext_def_list == NULL) {
        return;
    }

    analyze_ext_def(context, ast_child(ext_def_list, 0));
    analyze_ext_def_list(context, ast_child(ext_def_list, 1));
}

SemanticContext *semantic_analyze(AstNode *root) {
    SemanticContext *context;

    context = (SemanticContext *)calloc(1, sizeof(SemanticContext));
    if(context == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    context->next_anonymous_struct_id = 1;
    context->next_ir_var_id = 1;

    add_builtin_read(context);
    add_builtin_write(context);

    if(root != NULL) {
        analyze_ext_def_list(context, ast_child(root, 0));
    }
    return context;
}

void semantic_context_free(SemanticContext *context) {
    Symbol *symbol;
    Symbol *next_symbol;
    FunctionSymbol *function;
    FunctionSymbol *next_function;
    Type *type;
    Type *next_type;
    Field *field;
    Field *next_field;

    if(context == NULL) {
        return;
    }

    symbol = context->variables;
    while(symbol != NULL) {
        next_symbol = symbol->next;
        free(symbol->name);
        free(symbol->ir_name);
        free(symbol);
        symbol = next_symbol;
    }

    function = context->functions;
    while(function != NULL) {
        next_function = function->next;
        free(function->name);
        field = function->params;
        while(field != NULL) {
            next_field = field->next;
            free(field->name);
            free(field);
            field = next_field;
        }
        free(function);
        function = next_function;
    }

    type = context->allocated_types;
    while(type != NULL) {
        next_type = type->alloc_next;
        if(type->kind == TYPE_STRUCT) {
            field = type->info.structure.fields;
            while(field != NULL) {
                next_field = field->next;
                free(field->name);
                free(field);
                field = next_field;
            }
            free(type->info.structure.name);
        }
        free(type);
        type = next_type;
    }

    free(context);
}
