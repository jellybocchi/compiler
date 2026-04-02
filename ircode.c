#include "ircode.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *ircode_strdup(const char *source) {
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

static void trim_line(char *line) {
    size_t length;

    if(line == NULL) {
        return;
    }

    length = strlen(line);
    while(length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        --length;
    }
}

static IROperand parse_operand(const char *token) {
    if(token == NULL || token[0] == '\0') {
        return ircode_make_none();
    }

    if(token[0] == '#') {
        return ircode_make_constant((int)strtol(token + 1, NULL, 10));
    }

    if(token[0] == '&') {
        return ircode_make_address(token + 1);
    }

    if(token[0] == '*') {
        return ircode_make_deref(token + 1);
    }

    return ircode_make_variable(token);
}

IRModule *ircode_new_module(void) {
    IRModule *module;

    module = (IRModule *)calloc(1, sizeof(IRModule));
    if(module == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    return module;
}

void ircode_append_function(IRModule *module, const char *name) {
    IRFunction *functions;
    IRFunction *function;

    if(module->count == module->capacity) {
        module->capacity = module->capacity == 0 ? 4 : module->capacity * 2;
        functions = (IRFunction *)realloc(
            module->functions,
            (size_t)module->capacity * sizeof(IRFunction)
        );
        if(functions == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(1);
        }
        module->functions = functions;
    }

    function = &module->functions[module->count];
    memset(function, 0, sizeof(IRFunction));
    function->name = ircode_strdup(name);
    ++module->count;
}

IRFunction *ircode_last_function(IRModule *module) {
    if(module == NULL || module->count == 0) {
        return NULL;
    }
    return &module->functions[module->count - 1];
}

void ircode_append_code(IRFunction *function, IRCode code) {
    IRCode *codes;

    if(function->count == function->capacity) {
        function->capacity = function->capacity == 0 ? 16 : function->capacity * 2;
        codes = (IRCode *)realloc(
            function->codes,
            (size_t)function->capacity * sizeof(IRCode)
        );
        if(codes == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(1);
        }
        function->codes = codes;
    }

    function->codes[function->count] = code;
    ++function->count;
}

IROperand ircode_make_none(void) {
    IROperand operand;

    memset(&operand, 0, sizeof(IROperand));
    operand.kind = IR_OPERAND_NONE;
    return operand;
}

IROperand ircode_make_variable(const char *name) {
    IROperand operand;

    operand = ircode_make_none();
    operand.kind = IR_OPERAND_VARIABLE;
    operand.name = ircode_strdup(name);
    return operand;
}

IROperand ircode_make_constant(int value) {
    IROperand operand;

    operand = ircode_make_none();
    operand.kind = IR_OPERAND_CONSTANT;
    operand.value = value;
    return operand;
}

IROperand ircode_make_address(const char *name) {
    IROperand operand;

    operand = ircode_make_none();
    operand.kind = IR_OPERAND_ADDRESS;
    operand.name = ircode_strdup(name);
    return operand;
}

IROperand ircode_make_deref(const char *name) {
    IROperand operand;

    operand = ircode_make_none();
    operand.kind = IR_OPERAND_DEREF;
    operand.name = ircode_strdup(name);
    return operand;
}

IROperand ircode_copy_operand(IROperand operand) {
    IROperand copy;

    copy = operand;
    if(operand.name != NULL) {
        copy.name = ircode_strdup(operand.name);
    }
    return copy;
}

void ircode_free_operand(IROperand *operand) {
    if(operand == NULL) {
        return;
    }

    free(operand->name);
    operand->name = NULL;
    operand->kind = IR_OPERAND_NONE;
    operand->value = 0;
}

IRCode ircode_copy_code(const IRCode *code) {
    IRCode copy;

    memset(&copy, 0, sizeof(IRCode));
    copy.kind = code->kind;
    copy.result = ircode_copy_operand(code->result);
    copy.op1 = ircode_copy_operand(code->op1);
    copy.op2 = ircode_copy_operand(code->op2);
    copy.relop = ircode_strdup(code->relop);
    copy.name = ircode_strdup(code->name);
    copy.size = code->size;
    return copy;
}

void ircode_free_code(IRCode *code) {
    if(code == NULL) {
        return;
    }

    ircode_free_operand(&code->result);
    ircode_free_operand(&code->op1);
    ircode_free_operand(&code->op2);
    free(code->relop);
    free(code->name);
    memset(code, 0, sizeof(IRCode));
}

int ircode_operand_equal(IROperand left, IROperand right) {
    if(left.kind != right.kind) {
        return 0;
    }

    if(left.kind == IR_OPERAND_CONSTANT) {
        return left.value == right.value;
    }

    if(left.kind == IR_OPERAND_NONE) {
        return 1;
    }

    if(left.name == NULL || right.name == NULL) {
        return left.name == right.name;
    }

    return strcmp(left.name, right.name) == 0;
}

int ircode_is_value_operand(IROperand operand) {
    return operand.kind == IR_OPERAND_VARIABLE ||
        operand.kind == IR_OPERAND_CONSTANT;
}

static IRCode parse_line(char *line) {
    IRCode code;
    char *tokens[8];
    int token_count;
    char *saveptr;
    char *token;

    memset(&code, 0, sizeof(IRCode));
    code.kind = IR_NOP;

    token_count = 0;
    token = strtok_r(line, " \t", &saveptr);
    while(token != NULL && token_count < 8) {
        tokens[token_count] = token;
        ++token_count;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    if(token_count == 0) {
        return code;
    }

    if(strcmp(tokens[0], "FUNCTION") == 0 && token_count >= 2) {
        code.kind = IR_FUNCTION;
        code.name = ircode_strdup(tokens[1]);
        return code;
    }

    if(strcmp(tokens[0], "LABEL") == 0 && token_count >= 2) {
        code.kind = IR_LABEL;
        code.name = ircode_strdup(tokens[1]);
        return code;
    }

    if(strcmp(tokens[0], "GOTO") == 0 && token_count >= 2) {
        code.kind = IR_GOTO;
        code.name = ircode_strdup(tokens[1]);
        return code;
    }

    if(strcmp(tokens[0], "IF") == 0 && token_count >= 6) {
        code.kind = IR_IF_GOTO;
        code.op1 = parse_operand(tokens[1]);
        code.relop = ircode_strdup(tokens[2]);
        code.op2 = parse_operand(tokens[3]);
        code.name = ircode_strdup(tokens[5]);
        return code;
    }

    if(strcmp(tokens[0], "RETURN") == 0 && token_count >= 2) {
        code.kind = IR_RETURN;
        code.op1 = parse_operand(tokens[1]);
        return code;
    }

    if(strcmp(tokens[0], "DEC") == 0 && token_count >= 3) {
        code.kind = IR_DEC;
        code.result = parse_operand(tokens[1]);
        code.size = (int)strtol(tokens[2], NULL, 10);
        return code;
    }

    if(strcmp(tokens[0], "ARG") == 0 && token_count >= 2) {
        code.kind = IR_ARG;
        code.op1 = parse_operand(tokens[1]);
        return code;
    }

    if(strcmp(tokens[0], "PARAM") == 0 && token_count >= 2) {
        code.kind = IR_PARAM;
        code.result = parse_operand(tokens[1]);
        return code;
    }

    if(strcmp(tokens[0], "READ") == 0 && token_count >= 2) {
        code.kind = IR_READ;
        code.result = parse_operand(tokens[1]);
        return code;
    }

    if(strcmp(tokens[0], "WRITE") == 0 && token_count >= 2) {
        code.kind = IR_WRITE;
        code.op1 = parse_operand(tokens[1]);
        return code;
    }

    if(token_count >= 3 && strcmp(tokens[1], ":=") == 0) {
        code.result = parse_operand(tokens[0]);

        if(token_count == 4 && strcmp(tokens[2], "CALL") == 0) {
            code.kind = IR_CALL;
            code.name = ircode_strdup(tokens[3]);
            return code;
        }

        if(token_count == 3) {
            code.kind = IR_ASSIGN;
            code.op1 = parse_operand(tokens[2]);
            return code;
        }

        if(token_count == 5) {
            if(strcmp(tokens[3], "+") == 0) {
                code.kind = IR_ADD;
            } else if(strcmp(tokens[3], "-") == 0) {
                code.kind = IR_SUB;
            } else if(strcmp(tokens[3], "*") == 0) {
                code.kind = IR_MUL;
            } else if(strcmp(tokens[3], "/") == 0) {
                code.kind = IR_DIV;
            }
            code.op1 = parse_operand(tokens[2]);
            code.op2 = parse_operand(tokens[4]);
            return code;
        }
    }

    return code;
}

IRModule *ircode_parse_file(FILE *input) {
    IRModule *module;
    IRFunction *function;
    char line_buffer[512];

    module = ircode_new_module();
    function = NULL;

    while(fgets(line_buffer, sizeof(line_buffer), input) != NULL) {
        IRCode code;

        trim_line(line_buffer);
        code = parse_line(line_buffer);
        if(code.kind == IR_NOP) {
            continue;
        }

        if(code.kind == IR_FUNCTION) {
            ircode_append_function(module, code.name);
            function = ircode_last_function(module);
            ircode_free_code(&code);
            continue;
        }

        if(function == NULL) {
            ircode_free_code(&code);
            continue;
        }

        ircode_append_code(function, code);
    }

    return module;
}

IRModule *ircode_parse_string(const char *text) {
    IRModule *module;
    FILE *stream;

    stream = fmemopen((void *)text, strlen(text), "r");
    if(stream == NULL) {
        return NULL;
    }
    module = ircode_parse_file(stream);
    fclose(stream);
    return module;
}

static void print_operand(FILE *output, IROperand operand) {
    switch(operand.kind) {
        case IR_OPERAND_VARIABLE:
            fprintf(output, "%s", operand.name);
            break;
        case IR_OPERAND_CONSTANT:
            fprintf(output, "#%d", operand.value);
            break;
        case IR_OPERAND_ADDRESS:
            fprintf(output, "&%s", operand.name);
            break;
        case IR_OPERAND_DEREF:
            fprintf(output, "*%s", operand.name);
            break;
        default:
            break;
    }
}

static void print_code(FILE *output, const IRCode *code) {
    switch(code->kind) {
        case IR_LABEL:
            fprintf(output, "LABEL %s :\n", code->name);
            break;
        case IR_ASSIGN:
            print_operand(output, code->result);
            fprintf(output, " := ");
            print_operand(output, code->op1);
            fprintf(output, "\n");
            break;
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV:
            print_operand(output, code->result);
            fprintf(output, " := ");
            print_operand(output, code->op1);
            if(code->kind == IR_ADD) {
                fprintf(output, " + ");
            } else if(code->kind == IR_SUB) {
                fprintf(output, " - ");
            } else if(code->kind == IR_MUL) {
                fprintf(output, " * ");
            } else {
                fprintf(output, " / ");
            }
            print_operand(output, code->op2);
            fprintf(output, "\n");
            break;
        case IR_GOTO:
            fprintf(output, "GOTO %s\n", code->name);
            break;
        case IR_IF_GOTO:
            fprintf(output, "IF ");
            print_operand(output, code->op1);
            fprintf(output, " %s ", code->relop);
            print_operand(output, code->op2);
            fprintf(output, " GOTO %s\n", code->name);
            break;
        case IR_RETURN:
            fprintf(output, "RETURN ");
            print_operand(output, code->op1);
            fprintf(output, "\n");
            break;
        case IR_DEC:
            fprintf(output, "DEC ");
            print_operand(output, code->result);
            fprintf(output, " %d\n", code->size);
            break;
        case IR_ARG:
            fprintf(output, "ARG ");
            print_operand(output, code->op1);
            fprintf(output, "\n");
            break;
        case IR_CALL:
            print_operand(output, code->result);
            fprintf(output, " := CALL %s\n", code->name);
            break;
        case IR_PARAM:
            fprintf(output, "PARAM ");
            print_operand(output, code->result);
            fprintf(output, "\n");
            break;
        case IR_READ:
            fprintf(output, "READ ");
            print_operand(output, code->result);
            fprintf(output, "\n");
            break;
        case IR_WRITE:
            fprintf(output, "WRITE ");
            print_operand(output, code->op1);
            fprintf(output, "\n");
            break;
        default:
            break;
    }
}

void ircode_print_module(FILE *output, const IRModule *module) {
    int i;
    int j;

    if(output == NULL || module == NULL) {
        return;
    }

    for(i = 0; i < module->count; ++i) {
        fprintf(output, "FUNCTION %s :\n", module->functions[i].name);
        for(j = 0; j < module->functions[i].count; ++j) {
            print_code(output, &module->functions[i].codes[j]);
        }
    }
}

void ircode_free_module(IRModule *module) {
    int i;
    int j;

    if(module == NULL) {
        return;
    }

    for(i = 0; i < module->count; ++i) {
        free(module->functions[i].name);
        for(j = 0; j < module->functions[i].count; ++j) {
            ircode_free_code(&module->functions[i].codes[j]);
        }
        free(module->functions[i].codes);
    }
    free(module->functions);
    free(module);
}
