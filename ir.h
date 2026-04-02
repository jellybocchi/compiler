#ifndef IR_H
#define IR_H

#include <stdio.h>
#include "ast.h"
#include "semantic.h"

typedef struct IRProgram IRProgram;
IRProgram *ir_generate(AstNode *root, SemanticContext *context);
void ir_print_program(FILE *output, const IRProgram *program);
void ir_program_free(IRProgram *program);

#endif
