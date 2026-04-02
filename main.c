#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "error.h"
#include "ir.h"
#include "ircode.h"
#include "mips.h"
#include "optimizer.h"
#include "semantic.h"

extern FILE *yyin;
extern int yyparse(void);
extern AstNode *syntax_tree;

typedef enum {
    MODE_LAB1 = 1,
    MODE_LAB2 = 2,
    MODE_LAB3 = 3,
    MODE_LAB4 = 4,
    MODE_LAB5 = 5
} CompilerMode;

static void print_usage(const char *program) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --lab1 <input>\n", program);
    fprintf(stderr, "  %s <input>\n", program);
    fprintf(stderr, "  %s <input> <output.ir>\n", program);
    fprintf(stderr, "  %s --lab2 <input>\n", program);
    fprintf(stderr, "  %s --lab3 <input> <output.ir>\n", program);
    fprintf(stderr, "  %s --lab4 <input> <output.s>\n", program);
    fprintf(stderr, "  %s --lab5 <input.ir> <output.ir>\n", program);
}

static int open_input(const char *path) {
    yyin = fopen(path, "r");
    if(yyin == NULL) {
        perror(path);
        return 0;
    }
    return 1;
}

static int run_lab3(const char *output_path) {
    SemanticContext *context;
    IRProgram *program;
    FILE *output;

    context = semantic_analyze(syntax_tree);
    if(has_error()) {
        semantic_context_free(context);
        return 1;
    }

    program = ir_generate(syntax_tree, context);
    if(has_error() || program == NULL) {
        ir_program_free(program);
        semantic_context_free(context);
        return 1;
    }

    output = fopen(output_path, "w");
    if(output == NULL) {
        perror(output_path);
        ir_program_free(program);
        semantic_context_free(context);
        return 1;
    }

    ir_print_program(output, program);
    fclose(output);
    ir_program_free(program);
    semantic_context_free(context);
    return 0;
}

static IRModule *generate_source_ir_module(void) {
    SemanticContext *context;
    IRProgram *program;
    FILE *stream;
    char *buffer;
    size_t size;
    IRModule *module;

    context = semantic_analyze(syntax_tree);
    if(has_error()) {
        semantic_context_free(context);
        return NULL;
    }

    program = ir_generate(syntax_tree, context);
    if(has_error() || program == NULL) {
        ir_program_free(program);
        semantic_context_free(context);
        return NULL;
    }

    buffer = NULL;
    size = 0;
    stream = open_memstream(&buffer, &size);
    if(stream == NULL) {
        ir_program_free(program);
        semantic_context_free(context);
        return NULL;
    }

    ir_print_program(stream, program);
    fclose(stream);

    module = ircode_parse_string(buffer);
    free(buffer);
    ir_program_free(program);
    semantic_context_free(context);
    return module;
}

static int run_lab4(const char *output_path) {
    IRModule *module;
    FILE *output;

    module = generate_source_ir_module();
    if(module == NULL) {
        return 1;
    }

    output = fopen(output_path, "w");
    if(output == NULL) {
        perror(output_path);
        ircode_free_module(module);
        return 1;
    }

    mips_generate(output, module);
    fclose(output);
    ircode_free_module(module);
    return 0;
}

static int run_lab5(const char *input_path, const char *output_path) {
    FILE *input;
    FILE *output;
    IRModule *module;

    input = fopen(input_path, "r");
    if(input == NULL) {
        perror(input_path);
        return 1;
    }

    module = ircode_parse_file(input);
    fclose(input);
    if(module == NULL) {
        return 1;
    }

    optimize_ir_module(module);

    output = fopen(output_path, "w");
    if(output == NULL) {
        perror(output_path);
        ircode_free_module(module);
        return 1;
    }

    ircode_print_module(output, module);
    fclose(output);
    ircode_free_module(module);
    return 0;
}

int main(int argc, char **argv) {
    CompilerMode mode;
    const char *input_path;
    const char *output_path = NULL;
    SemanticContext *context = NULL;
    int exit_code = 0;

    error_reset();

    if(argc == 2) {
        mode = MODE_LAB2;
        input_path = argv[1];
    } else if(argc == 3) {
        if(strcmp(argv[1], "--lab1") == 0) {
            mode = MODE_LAB1;
            input_path = argv[2];
        } else if(strcmp(argv[1], "--lab2") == 0) {
            mode = MODE_LAB2;
            input_path = argv[2];
        } else {
            mode = MODE_LAB3;
            input_path = argv[1];
            output_path = argv[2];
        }
    } else if(argc == 4 && strcmp(argv[1], "--lab3") == 0) {
        mode = MODE_LAB3;
        input_path = argv[2];
        output_path = argv[3];
    } else if(argc == 4 && strcmp(argv[1], "--lab4") == 0) {
        mode = MODE_LAB4;
        input_path = argv[2];
        output_path = argv[3];
    } else if(argc == 4 && strcmp(argv[1], "--lab5") == 0) {
        mode = MODE_LAB5;
        input_path = argv[2];
        output_path = argv[3];
    } else {
        print_usage(argv[0]);
        return 1;
    }

    if(mode == MODE_LAB5) {
        return run_lab5(input_path, output_path);
    }

    if(!open_input(input_path)) {
        return 1;
    }

    yyparse();
    if(has_error()) {
        exit_code = 1;
        goto cleanup;
    }

    if(mode == MODE_LAB1) {
        ast_print(syntax_tree, 0);
        goto cleanup;
    }

    if(mode == MODE_LAB2) {
        context = semantic_analyze(syntax_tree);
        if(has_error()) {
            exit_code = 1;
        }
        goto cleanup;
    }

    if(mode == MODE_LAB4) {
        exit_code = run_lab4(output_path);
    } else if(mode == MODE_LAB3) {
        exit_code = run_lab3(output_path);
    }

cleanup:
    semantic_context_free(context);
    ast_free(syntax_tree);
    syntax_tree = NULL;
    if(yyin != NULL && yyin != stdin) {
        fclose(yyin);
    }
    return exit_code;
}
