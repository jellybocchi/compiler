#ifndef IRCODE_H
#define IRCODE_H

#include <stdio.h>

typedef enum {
    IR_OPERAND_NONE = 0,
    IR_OPERAND_VARIABLE = 1,
    IR_OPERAND_CONSTANT = 2,
    IR_OPERAND_ADDRESS = 3,
    IR_OPERAND_DEREF = 4
} IROperandKind;

typedef enum {
    IR_LABEL = 0,
    IR_FUNCTION = 1,
    IR_ASSIGN = 2,
    IR_ADD = 3,
    IR_SUB = 4,
    IR_MUL = 5,
    IR_DIV = 6,
    IR_GOTO = 7,
    IR_IF_GOTO = 8,
    IR_RETURN = 9,
    IR_DEC = 10,
    IR_ARG = 11,
    IR_CALL = 12,
    IR_PARAM = 13,
    IR_READ = 14,
    IR_WRITE = 15,
    IR_NOP = 16
} IRCodeKind;

typedef struct {
    IROperandKind kind;
    char *name;
    int value;
} IROperand;

typedef struct {
    IRCodeKind kind;
    IROperand result;
    IROperand op1;
    IROperand op2;
    char *relop;
    char *name;
    int size;
} IRCode;

typedef struct {
    char *name;
    IRCode *codes;
    int count;
    int capacity;
} IRFunction;

typedef struct {
    IRFunction *functions;
    int count;
    int capacity;
} IRModule;

IRModule *ircode_new_module(void);
void ircode_free_module(IRModule *module);

IRModule *ircode_parse_file(FILE *input);
IRModule *ircode_parse_string(const char *text);
void ircode_print_module(FILE *output, const IRModule *module);

void ircode_append_function(IRModule *module, const char *name);
IRFunction *ircode_last_function(IRModule *module);
void ircode_append_code(IRFunction *function, IRCode code);

IROperand ircode_make_none(void);
IROperand ircode_make_variable(const char *name);
IROperand ircode_make_constant(int value);
IROperand ircode_make_address(const char *name);
IROperand ircode_make_deref(const char *name);
IROperand ircode_copy_operand(IROperand operand);
void ircode_free_operand(IROperand *operand);
IRCode ircode_copy_code(const IRCode *code);
void ircode_free_code(IRCode *code);

int ircode_operand_equal(IROperand left, IROperand right);
int ircode_is_value_operand(IROperand operand);

#endif
