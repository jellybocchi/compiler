#include "ir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct IRLine {
    char *text;
    struct IRLine *next;
} IRLine;

struct IRProgram {
    IRLine *head;
    IRLine *tail;
    SemanticContext *context;
    int next_temp_id;
    int next_label_id;
    int failed;
};

static char *ir_strdup(const char *source) {
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

static char *ir_format(const char *format, ...) {
    va_list args;
    va_list args_copy;
    int length;
    char *buffer;

    va_start(args, format);
    va_copy(args_copy, args);
    length = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    buffer = (char *)malloc((size_t)length + 1);
    if(buffer == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    vsnprintf(buffer, (size_t)length + 1, format, args);
    va_end(args);
    return buffer;
}

static void ir_emit(IRProgram *program, const char *format, ...) {
    va_list args;
    va_list args_copy;
    int length;
    char *text;
    IRLine *line;

    if(program == NULL || program->failed) {
        return;
    }

    va_start(args, format);
    va_copy(args_copy, args);
    length = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    text = (char *)malloc((size_t)length + 1);
    if(text == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    vsnprintf(text, (size_t)length + 1, format, args);
    va_end(args);

    line = (IRLine *)calloc(1, sizeof(IRLine));
    if(line == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    line->text = text;

    if(program->head == NULL) {
        program->head = line;
        program->tail = line;
        return;
    }

    program->tail->next = line;
    program->tail = line;
}

static char *ir_new_temp(IRProgram *program) {
    char buffer[64];

    snprintf(buffer, sizeof(buffer), "t%d", program->next_temp_id);
    ++program->next_temp_id;
    return ir_strdup(buffer);
}

static char *ir_new_label(IRProgram *program) {
    char buffer[64];

    snprintf(buffer, sizeof(buffer), "label%d", program->next_label_id);
    ++program->next_label_id;
    return ir_strdup(buffer);
}

static char *ir_const(int value) {
    return ir_format("#%d", value);
}

static int ir_is_constant(const char *operand, int *value) {
    char *end;
    long number;

    if(operand == NULL || operand[0] != '#') {
        return 0;
    }

    number = strtol(operand + 1, &end, 10);
    if(*end != '\0') {
        return 0;
    }

    *value = (int)number;
    return 1;
}

static void ir_fail(IRProgram *program, const char *message) {
    if(program == NULL || program->failed) {
        return;
    }

    printf("%s\n", message);
    program->failed = 1;
}

static int type_is_ir_int(const Type *type) {
    if(type == NULL) {
        return 0;
    }
    if(type->kind != TYPE_BASIC) {
        return 0;
    }
    return type->info.basic == BASIC_INT;
}

static int type_is_ir_array(const Type *type) {
    if(type == NULL || type->kind != TYPE_ARRAY) {
        return 0;
    }
    if(type->info.array.element == NULL) {
        return 0;
    }
    return type_is_ir_int(type->info.array.element);
}

static int type_is_multi_dim_array(const Type *type) {
    if(type == NULL || type->kind != TYPE_ARRAY) {
        return 0;
    }
    if(type->info.array.element == NULL) {
        return 0;
    }
    return type->info.array.element->kind == TYPE_ARRAY;
}

static Symbol *find_symbol(IRProgram *program, const char *name) {
    return semantic_find_variable(program->context, name);
}

static FunctionSymbol *find_function(IRProgram *program, const char *name) {
    return semantic_find_function(program->context, name);
}

static char *symbol_ir_name(IRProgram *program, const char *name) {
    Symbol *symbol;

    symbol = find_symbol(program, name);
    if(symbol == NULL) {
        return NULL;
    }
    return ir_strdup(symbol->ir_name);
}

static Type *symbol_type(IRProgram *program, const char *name) {
    Symbol *symbol;

    symbol = find_symbol(program, name);
    if(symbol == NULL) {
        return NULL;
    }
    return symbol->type;
}

static const char *var_dec_name(AstNode *var_dec) {
    AstNode *current;

    current = var_dec;
    while(current != NULL && ast_child_count(current) != 1) {
        current = ast_child(current, 0);
    }
    if(current == NULL) {
        return NULL;
    }
    return ast_string(ast_child(current, 0));
}

static char *translate_exp(IRProgram *program, AstNode *exp);
static char *translate_addr(IRProgram *program, AstNode *exp);
static void translate_stmt(IRProgram *program, AstNode *stmt);
static void translate_compst(IRProgram *program, AstNode *comp_st);

static char *fold_binary_constant(
    const char *left,
    const char *operator_text,
    const char *right
) {
    int left_value;
    int right_value;
    int result;

    if(!ir_is_constant(left, &left_value) || !ir_is_constant(right, &right_value)) {
        return NULL;
    }

    if(strcmp(operator_text, "+") == 0) {
        result = left_value + right_value;
    } else if(strcmp(operator_text, "-") == 0) {
        result = left_value - right_value;
    } else if(strcmp(operator_text, "*") == 0) {
        result = left_value * right_value;
    } else {
        if(right_value == 0) {
            return NULL;
        }
        result = left_value / right_value;
    }

    return ir_const(result);
}

static char *translate_binary(
    IRProgram *program,
    AstNode *exp,
    const char *operator_text
) {
    char *left;
    char *right;
    char *folded;
    char *result;

    left = translate_exp(program, ast_child(exp, 0));
    right = translate_exp(program, ast_child(exp, 2));
    if(program->failed) {
        return NULL;
    }

    folded = fold_binary_constant(left, operator_text, right);
    if(folded != NULL) {
        return folded;
    }

    result = ir_new_temp(program);
    ir_emit(program, "%s := %s %s %s", result, left, operator_text, right);
    return result;
}

static void translate_cond(
    IRProgram *program,
    AstNode *exp,
    const char *true_label,
    const char *false_label
) {
    int child_count;
    AstNode *first;

    if(program->failed) {
        return;
    }

    child_count = ast_child_count(exp);
    first = ast_child(exp, 0);

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "RELOP")) {
        char *left;
        char *right;

        left = translate_exp(program, ast_child(exp, 0));
        right = translate_exp(program, ast_child(exp, 2));
        ir_emit(
            program,
            "IF %s %s %s GOTO %s",
            left,
            ast_string(ast_child(exp, 1)),
            right,
            true_label
        );
        ir_emit(program, "GOTO %s", false_label);
        return;
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "AND")) {
        char *middle_label;

        middle_label = ir_new_label(program);
        translate_cond(program, ast_child(exp, 0), middle_label, false_label);
        ir_emit(program, "LABEL %s :", middle_label);
        translate_cond(program, ast_child(exp, 2), true_label, false_label);
        return;
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "OR")) {
        char *middle_label;

        middle_label = ir_new_label(program);
        translate_cond(program, ast_child(exp, 0), true_label, middle_label);
        ir_emit(program, "LABEL %s :", middle_label);
        translate_cond(program, ast_child(exp, 2), true_label, false_label);
        return;
    }

    if(child_count == 2 && ast_is_name(first, "NOT")) {
        translate_cond(program, ast_child(exp, 1), false_label, true_label);
        return;
    }

    {
        char *value;

        value = translate_exp(program, exp);
        if(program->failed) {
            return;
        }
        ir_emit(program, "IF %s != #0 GOTO %s", value, true_label);
        ir_emit(program, "GOTO %s", false_label);
    }
}

static void translate_args(IRProgram *program, AstNode *args) {
    if(args == NULL || program->failed) {
        return;
    }

    if(ast_child_count(args) == 3) {
        translate_args(program, ast_child(args, 2));
    }
    ir_emit(program, "ARG %s", translate_exp(program, ast_child(args, 0)));
}

static char *translate_call(IRProgram *program, AstNode *exp) {
    AstNode *id_node;
    const char *name;
    char *result;

    id_node = ast_child(exp, 0);
    name = ast_string(id_node);

    if(strcmp(name, "read") == 0) {
        result = ir_new_temp(program);
        ir_emit(program, "READ %s", result);
        return result;
    }

    if(strcmp(name, "write") == 0) {
        AstNode *args;
        char *argument;

        args = ast_child(exp, 2);
        argument = translate_exp(program, ast_child(args, 0));
        ir_emit(program, "WRITE %s", argument);
        return ir_const(0);
    }

    if(ast_child_count(exp) == 4) {
        translate_args(program, ast_child(exp, 2));
    }

    result = ir_new_temp(program);
    ir_emit(program, "%s := CALL %s", result, name);
    return result;
}

static char *translate_addr(IRProgram *program, AstNode *exp) {
    int child_count;

    if(program->failed) {
        return NULL;
    }

    child_count = ast_child_count(exp);

    if(child_count == 1 && ast_is_name(ast_child(exp, 0), "ID")) {
        const char *name;
        Type *type;
        char *ir_name;

        name = ast_string(ast_child(exp, 0));
        type = symbol_type(program, name);
        ir_name = symbol_ir_name(program, name);

        if(type_is_ir_int(type)) {
            return ir_name;
        }
        if(type_is_ir_array(type)) {
            return ir_format("&%s", ir_name);
        }

        ir_fail(
            program,
            "Cannot translate: Code contains variables of structure type."
        );
        return NULL;
    }

    if(child_count == 4 && ast_is_name(ast_child(exp, 1), "LB")) {
        char *base_addr;
        char *index_value;
        char *offset;
        char *address;

        base_addr = translate_addr(program, ast_child(exp, 0));
        index_value = translate_exp(program, ast_child(exp, 2));
        offset = ir_new_temp(program);
        ir_emit(program, "%s := %s * #4", offset, index_value);
        address = ir_new_temp(program);
        ir_emit(program, "%s := %s + %s", address, base_addr, offset);
        return address;
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 0), "LP")) {
        return translate_addr(program, ast_child(exp, 1));
    }

    ir_fail(
        program,
        "Cannot translate: Code contains unsupported lvalue expression."
    );
    return NULL;
}

static char *translate_bool_value(IRProgram *program, AstNode *exp) {
    char *true_label;
    char *false_label;
    char *end_label;
    char *result;

    true_label = ir_new_label(program);
    false_label = ir_new_label(program);
    end_label = ir_new_label(program);
    result = ir_new_temp(program);

    translate_cond(program, exp, true_label, false_label);
    ir_emit(program, "LABEL %s :", true_label);
    ir_emit(program, "%s := #1", result);
    ir_emit(program, "GOTO %s", end_label);
    ir_emit(program, "LABEL %s :", false_label);
    ir_emit(program, "%s := #0", result);
    ir_emit(program, "LABEL %s :", end_label);
    return result;
}

static char *translate_exp(IRProgram *program, AstNode *exp) {
    int child_count;
    AstNode *first;

    if(program->failed) {
        return NULL;
    }

    child_count = ast_child_count(exp);
    first = ast_child(exp, 0);

    if(child_count == 1) {
        if(ast_is_name(first, "ID")) {
            const char *name;
            Type *type;

            name = ast_string(first);
            type = symbol_type(program, name);
            if(type_is_ir_int(type)) {
                return symbol_ir_name(program, name);
            }
            if(type_is_ir_array(type)) {
                return ir_format("&%s", symbol_ir_name(program, name));
            }
            ir_fail(
                program,
                "Cannot translate: Code contains variables of structure type."
            );
            return NULL;
        }

        if(ast_is_name(first, "INT")) {
            return ir_const(ast_int(first));
        }

        ir_fail(
            program,
            "Cannot translate: Code contains floating-point values."
        );
        return NULL;
    }

    if(child_count == 2) {
        char *value;
        int constant_value;

        value = translate_exp(program, ast_child(exp, 1));
        if(program->failed) {
            return NULL;
        }

        if(ast_is_name(first, "MINUS")) {
            if(ir_is_constant(value, &constant_value)) {
                return ir_const(-constant_value);
            }

            {
                char *result;

                result = ir_new_temp(program);
                ir_emit(program, "%s := #0 - %s", result, value);
                return result;
            }
        }

        return translate_bool_value(program, exp);
    }

    if(child_count == 3 && ast_is_name(first, "LP")) {
        return translate_exp(program, ast_child(exp, 1));
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "ASSIGNOP")) {
        AstNode *left;
        char *right_value;

        left = ast_child(exp, 0);
        right_value = translate_exp(program, ast_child(exp, 2));
        if(program->failed) {
            return NULL;
        }

        if(ast_child_count(left) == 1 && ast_is_name(ast_child(left, 0), "ID")) {
            char *target;

            target = symbol_ir_name(program, ast_string(ast_child(left, 0)));
            ir_emit(program, "%s := %s", target, right_value);
            return target;
        }

        {
            char *address;

            address = translate_addr(program, left);
            ir_emit(program, "*%s := %s", address, right_value);
            return right_value;
        }
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
        const char *operator_text;

        if(ast_is_name(ast_child(exp, 1), "PLUS")) {
            operator_text = "+";
        } else if(ast_is_name(ast_child(exp, 1), "MINUS")) {
            operator_text = "-";
        } else if(ast_is_name(ast_child(exp, 1), "STAR")) {
            operator_text = "*";
        } else {
            operator_text = "/";
        }

        return translate_binary(program, exp, operator_text);
    }

    if(
        child_count == 3 &&
        (
            ast_is_name(ast_child(exp, 1), "RELOP") ||
            ast_is_name(ast_child(exp, 1), "AND") ||
            ast_is_name(ast_child(exp, 1), "OR")
        )
    ) {
        return translate_bool_value(program, exp);
    }

    if(child_count == 3 && ast_is_name(ast_child(exp, 1), "LP")) {
        return translate_call(program, exp);
    }

    if(child_count == 4 && ast_is_name(ast_child(exp, 1), "LP")) {
        return translate_call(program, exp);
    }

    if(child_count == 4 && ast_is_name(ast_child(exp, 1), "LB")) {
        char *address;
        char *result;

        address = translate_addr(program, exp);
        result = ir_new_temp(program);
        ir_emit(program, "%s := *%s", result, address);
        return result;
    }

    ir_fail(
        program,
        "Cannot translate: Code contains unsupported expression."
    );
    return NULL;
}

static void translate_dec(IRProgram *program, AstNode *dec) {
    const char *name;
    Type *type;

    name = var_dec_name(ast_child(dec, 0));
    if(name == NULL) {
        return;
    }

    type = symbol_type(program, name);
    if(type_is_multi_dim_array(type)) {
        ir_fail(
            program,
            "Cannot translate: Code contains variables of multi-dimensional "
            "array type or parameters of array type."
        );
        return;
    }

    if(type_is_ir_array(type)) {
        ir_emit(
            program,
            "DEC %s %d",
            symbol_ir_name(program, name),
            semantic_type_size(type)
        );
    } else if(!type_is_ir_int(type)) {
        if(type != NULL && type->kind == TYPE_STRUCT) {
            ir_fail(
                program,
                "Cannot translate: Code contains variables or parameters of structure type."
            );
        } else {
            ir_fail(
                program,
                "Cannot translate: Code contains floating-point values."
            );
        }
        return;
    }

    if(ast_child_count(dec) == 3) {
        if(!type_is_ir_int(type)) {
            ir_fail(
                program,
                "Cannot translate: Code contains unsupported array initialization."
            );
            return;
        }

        ir_emit(
            program,
            "%s := %s",
            symbol_ir_name(program, name),
            translate_exp(program, ast_child(dec, 2))
        );
    }
}

static void translate_dec_list(IRProgram *program, AstNode *dec_list) {
    if(dec_list == NULL || program->failed) {
        return;
    }

    translate_dec(program, ast_child(dec_list, 0));
    if(ast_child_count(dec_list) == 3) {
        translate_dec_list(program, ast_child(dec_list, 2));
    }
}

static void translate_def_list(IRProgram *program, AstNode *def_list) {
    AstNode *def;
    AstNode *specifier;
    AstNode *type_node;

    if(def_list == NULL || ast_is_empty(def_list) || program->failed) {
        return;
    }

    def = ast_child(def_list, 0);
    if(ast_is_empty(def)) {
        return;
    }
    specifier = ast_child(def, 0);
    type_node = ast_child(specifier, 0);

    if(!ast_is_name(type_node, "TYPE")) {
        ir_fail(
            program,
            "Cannot translate: Code contains variables or parameters of structure type."
        );
        return;
    }

    if(strcmp(ast_string(type_node), "int") != 0) {
        ir_fail(
            program,
            "Cannot translate: Code contains floating-point values."
        );
        return;
    }

    translate_dec_list(program, ast_child(def, 1));
    translate_def_list(program, ast_child(def_list, 1));
}

static void translate_stmt_list(IRProgram *program, AstNode *stmt_list) {
    if(stmt_list == NULL || program->failed) {
        return;
    }

    translate_stmt(program, ast_child(stmt_list, 0));
    translate_stmt_list(program, ast_child(stmt_list, 1));
}

static void translate_stmt(IRProgram *program, AstNode *stmt) {
    int child_count;
    AstNode *first;

    if(stmt == NULL || program->failed) {
        return;
    }

    child_count = ast_child_count(stmt);
    first = ast_child(stmt, 0);

    if(child_count == 1) {
        translate_compst(program, first);
        return;
    }

    if(child_count == 2) {
        translate_exp(program, first);
        return;
    }

    if(ast_is_name(first, "RETURN")) {
        ir_emit(program, "RETURN %s", translate_exp(program, ast_child(stmt, 1)));
        return;
    }

    if(ast_is_name(first, "IF")) {
        char *true_label;
        char *false_label;
        char *end_label;

        true_label = ir_new_label(program);
        false_label = ir_new_label(program);

        if(child_count == 5) {
            translate_cond(program, ast_child(stmt, 2), true_label, false_label);
            ir_emit(program, "LABEL %s :", true_label);
            translate_stmt(program, ast_child(stmt, 4));
            ir_emit(program, "LABEL %s :", false_label);
            return;
        }

        end_label = ir_new_label(program);
        translate_cond(program, ast_child(stmt, 2), true_label, false_label);
        ir_emit(program, "LABEL %s :", true_label);
        translate_stmt(program, ast_child(stmt, 4));
        ir_emit(program, "GOTO %s", end_label);
        ir_emit(program, "LABEL %s :", false_label);
        translate_stmt(program, ast_child(stmt, 6));
        ir_emit(program, "LABEL %s :", end_label);
        return;
    }

    if(ast_is_name(first, "WHILE")) {
        char *start_label;
        char *body_label;
        char *end_label;

        start_label = ir_new_label(program);
        body_label = ir_new_label(program);
        end_label = ir_new_label(program);

        ir_emit(program, "LABEL %s :", start_label);
        translate_cond(program, ast_child(stmt, 2), body_label, end_label);
        ir_emit(program, "LABEL %s :", body_label);
        translate_stmt(program, ast_child(stmt, 4));
        ir_emit(program, "GOTO %s", start_label);
        ir_emit(program, "LABEL %s :", end_label);
    }
}

static void translate_compst(IRProgram *program, AstNode *comp_st) {
    if(comp_st == NULL || program->failed) {
        return;
    }

    translate_def_list(program, ast_child(comp_st, 1));
    translate_stmt_list(program, ast_child(comp_st, 2));
}

static void emit_params(IRProgram *program, Field *params) {
    Field *field;

    for(field = params; field != NULL; field = field->next) {
        Symbol *symbol;

        if(type_is_multi_dim_array(field->type)) {
            ir_fail(
                program,
                "Cannot translate: Code contains variables of multi-dimensional "
                "array type or parameters of array type."
            );
            return;
        }

        if(!type_is_ir_int(field->type)) {
            if(type_is_ir_array(field->type)) {
                ir_fail(
                    program,
                    "Cannot translate: Code contains variables of "
                    "multi-dimensional array type or parameters of array type."
                );
            } else if(field->type != NULL && field->type->kind == TYPE_STRUCT) {
                ir_fail(
                    program,
                    "Cannot translate: Code contains variables or parameters of structure type."
                );
            } else {
                ir_fail(
                    program,
                    "Cannot translate: Code contains floating-point values."
                );
            }
            return;
        }

        symbol = find_symbol(program, field->name);
        if(symbol == NULL) {
            ir_fail(program, "Cannot translate: Missing function parameter symbol.");
            return;
        }

        ir_emit(program, "PARAM %s", symbol->ir_name);
    }
}

static void translate_function_extdef(IRProgram *program, AstNode *ext_def) {
    AstNode *fun_dec;
    AstNode *id_node;
    FunctionSymbol *function;

    fun_dec = ast_child(ext_def, 1);
    id_node = ast_child(fun_dec, 0);
    function = find_function(program, ast_string(id_node));
    if(function == NULL) {
        ir_fail(program, "Cannot translate: Missing function definition.");
        return;
    }

    if(!type_is_ir_int(function->return_type)) {
        ir_fail(
            program,
            "Cannot translate: Code contains unsupported function return type."
        );
        return;
    }

    ir_emit(program, "FUNCTION %s :", function->name);
    emit_params(program, function->params);
    translate_compst(program, ast_child(ext_def, 2));
}

static void translate_ext_def_list(IRProgram *program, AstNode *ext_def_list) {
    AstNode *ext_def;

    if(ext_def_list == NULL || program->failed) {
        return;
    }

    ext_def = ast_child(ext_def_list, 0);
    if(ext_def != NULL) {
        if(
            ast_child_count(ext_def) == 3 &&
            ast_is_name(ast_child(ext_def, 1), "ExtDecList")
        ) {
            ir_fail(program, "Cannot translate: Code contains global variables.");
            return;
        }

        if(
            ast_child_count(ext_def) == 3 &&
            ast_is_name(ast_child(ext_def, 1), "FunDec")
        ) {
            translate_function_extdef(program, ext_def);
        }
    }

    translate_ext_def_list(program, ast_child(ext_def_list, 1));
}

IRProgram *ir_generate(AstNode *root, SemanticContext *context) {
    IRProgram *program;

    program = (IRProgram *)calloc(1, sizeof(IRProgram));
    if(program == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    program->context = context;
    program->next_temp_id = 1;
    program->next_label_id = 1;

    if(root != NULL) {
        translate_ext_def_list(program, ast_child(root, 0));
    }

    return program;
}

void ir_print_program(FILE *output, const IRProgram *program) {
    IRLine *line;

    if(output == NULL || program == NULL || program->failed) {
        return;
    }

    for(line = program->head; line != NULL; line = line->next) {
        fprintf(output, "%s\n", line->text);
    }
}

void ir_program_free(IRProgram *program) {
    IRLine *line;
    IRLine *next_line;

    if(program == NULL) {
        return;
    }

    line = program->head;
    while(line != NULL) {
        next_line = line->next;
        free(line->text);
        free(line);
        line = next_line;
    }

    free(program);
}
