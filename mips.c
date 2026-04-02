#include "mips.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    int offset;
    int size;
    int is_array;
} Slot;

typedef struct {
    Slot *slots;
    int count;
    int capacity;
    int frame_size;
    int param_count;
} FrameInfo;

static char *mips_strdup(const char *source) {
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

static int frame_find_slot(const FrameInfo *frame, const char *name) {
    int i;

    if(name == NULL) {
        return -1;
    }

    for(i = 0; i < frame->count; ++i) {
        if(strcmp(frame->slots[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void frame_add_slot(FrameInfo *frame, const char *name, int size, int is_array) {
    Slot *slots;
    int aligned_size;
    int index;

    if(name == NULL || frame_find_slot(frame, name) >= 0) {
        return;
    }

    if(frame->count == frame->capacity) {
        frame->capacity = frame->capacity == 0 ? 16 : frame->capacity * 2;
        slots = (Slot *)realloc(frame->slots, (size_t)frame->capacity * sizeof(Slot));
        if(slots == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(1);
        }
        frame->slots = slots;
    }

    aligned_size = size;
    if(aligned_size % 4 != 0) {
        aligned_size += 4 - aligned_size % 4;
    }

    index = frame->count;
    frame->slots[index].name = mips_strdup(name);
    frame->slots[index].offset = frame->frame_size;
    frame->slots[index].size = aligned_size;
    frame->slots[index].is_array = is_array;
    frame->frame_size += aligned_size;
    ++frame->count;
}

static void collect_operand_slot(FrameInfo *frame, IROperand operand) {
    if(
        operand.kind == IR_OPERAND_VARIABLE ||
        operand.kind == IR_OPERAND_DEREF
    ) {
        frame_add_slot(frame, operand.name, 4, 0);
    }
}

static FrameInfo build_frame(const IRFunction *function) {
    FrameInfo frame;
    int i;

    memset(&frame, 0, sizeof(FrameInfo));

    for(i = 0; i < function->count; ++i) {
        const IRCode *code;

        code = &function->codes[i];
        if(code->kind == IR_DEC) {
            frame_add_slot(&frame, code->result.name, code->size, 1);
        } else {
            collect_operand_slot(&frame, code->result);
        }
        collect_operand_slot(&frame, code->op1);
        collect_operand_slot(&frame, code->op2);
        if(code->kind == IR_PARAM) {
            ++frame.param_count;
        }
    }

    frame.frame_size += 8;
    if(frame.frame_size % 4 != 0) {
        frame.frame_size += 4 - frame.frame_size % 4;
    }
    return frame;
}

static void free_frame(FrameInfo *frame) {
    int i;

    for(i = 0; i < frame->count; ++i) {
        free(frame->slots[i].name);
    }
    free(frame->slots);
    memset(frame, 0, sizeof(FrameInfo));
}

static Slot *get_slot(FrameInfo *frame, const char *name) {
    int index;

    index = frame_find_slot(frame, name);
    if(index < 0) {
        return NULL;
    }
    return &frame->slots[index];
}

static void emit_load_operand(
    FILE *output,
    FrameInfo *frame,
    IROperand operand,
    const char *reg
) {
    Slot *slot;

    if(operand.kind == IR_OPERAND_CONSTANT) {
        fprintf(output, "  li %s, %d\n", reg, operand.value);
        return;
    }

    if(operand.kind == IR_OPERAND_VARIABLE) {
        slot = get_slot(frame, operand.name);
        if(slot != NULL && slot->is_array) {
            fprintf(output, "  addi %s, $fp, %d\n", reg, slot->offset);
        } else {
            fprintf(output, "  lw %s, %d($fp)\n", reg, slot->offset);
        }
        return;
    }

    if(operand.kind == IR_OPERAND_ADDRESS) {
        slot = get_slot(frame, operand.name);
        fprintf(output, "  addi %s, $fp, %d\n", reg, slot->offset);
        return;
    }

    if(operand.kind == IR_OPERAND_DEREF) {
        slot = get_slot(frame, operand.name);
        fprintf(output, "  lw %s, %d($fp)\n", reg, slot->offset);
        fprintf(output, "  lw %s, 0(%s)\n", reg, reg);
    }
}

static void emit_store_result(FILE *output,FrameInfo *frame,IROperand operand,const char *reg,const char *temp_reg) {
    Slot *slot;
    if(operand.kind == IR_OPERAND_VARIABLE) {
        slot = get_slot(frame, operand.name);
        fprintf(output, "  sw %s, %d($fp)\n", reg, slot->offset);
        return;
    }
    if(operand.kind == IR_OPERAND_DEREF) {
        slot = get_slot(frame, operand.name);
        fprintf(output, "  lw %s, %d($fp)\n", temp_reg, slot->offset);
        fprintf(output, "  sw %s, 0(%s)\n", reg, temp_reg);
    }
}

static void emit_read_write_helpers(FILE *output) {
    fprintf(output, ".data\n");
    fprintf(output, "_prompt: .asciiz \"Enter an integer:\"\n");
    fprintf(output, "_ret: .asciiz \"\\n\"\n");
    fprintf(output, ".globl main\n");
    fprintf(output, ".text\n");
    fprintf(output, "read:\n");
    fprintf(output, "  li $v0, 4\n");
    fprintf(output, "  la $a0, _prompt\n");
    fprintf(output, "  syscall\n");
    fprintf(output, "  li $v0, 5\n");
    fprintf(output, "  syscall\n");
    fprintf(output, "  jr $ra\n");
    fprintf(output, "write:\n");
    fprintf(output, "  li $v0, 1\n");
    fprintf(output, "  syscall\n");
    fprintf(output, "  li $v0, 4\n");
    fprintf(output, "  la $a0, _ret\n");
    fprintf(output, "  syscall\n");
    fprintf(output, "  move $v0, $zero\n");
    fprintf(output, "  jr $ra\n");
}

static void emit_function(FILE *output, const IRFunction *function, FrameInfo *frame) {
    int i;
    int pending_args;
    int param_index;
    int ra_offset;
    int fp_offset;
    ra_offset = frame->frame_size - 4;
    fp_offset = frame->frame_size - 8;
    pending_args = 0;
    param_index = 0;
    fprintf(output, "%s:\n", function->name);
    fprintf(output, "  addi $sp, $sp, -%d\n", frame->frame_size);
    fprintf(output, "  sw $fp, %d($sp)\n", fp_offset);
    fprintf(output, "  sw $ra, %d($sp)\n", ra_offset);
    fprintf(output, "  move $fp, $sp\n");

    for(i = 0; i < function->count; ++i) {
        const IRCode *code;
        Slot *slot;
        code = &function->codes[i];
        if(code->kind == IR_LABEL) {
            fprintf(output, "%s:\n", code->name);
            continue;
        }

        if(code->kind == IR_PARAM) {
            slot = get_slot(frame, code->result.name);
            fprintf(
                output,
                "  lw $t0, %d($fp)\n",
                frame->frame_size + param_index * 4
            );
            fprintf(output, "  sw $t0, %d($fp)\n", slot->offset);
            ++param_index;
            continue;
        }

        if(code->kind == IR_ASSIGN) {
            emit_load_operand(output, frame, code->op1, "$t0");
            emit_store_result(output, frame, code->result, "$t0", "$t1");
            continue;
        }

        if(
            code->kind == IR_ADD ||
            code->kind == IR_SUB ||
            code->kind == IR_MUL ||
            code->kind == IR_DIV
        ) {
            emit_load_operand(output, frame, code->op1, "$t0");
            emit_load_operand(output, frame, code->op2, "$t1");
            if(code->kind == IR_ADD) {
                fprintf(output, "  addu $t2, $t0, $t1\n");
            } else if(code->kind == IR_SUB) {
                fprintf(output, "  subu $t2, $t0, $t1\n");
            } else if(code->kind == IR_MUL) {
                fprintf(output, "  mul $t2, $t0, $t1\n");
            } else {
                fprintf(output, "  div $t0, $t1\n");
                fprintf(output, "  mflo $t2\n");
            }
            emit_store_result(output, frame, code->result, "$t2", "$t3");
            continue;
        }
        if(code->kind == IR_GOTO) {
            fprintf(output, "  j %s\n", code->name);
            continue;
        }
        if(code->kind == IR_IF_GOTO) {
            emit_load_operand(output, frame, code->op1, "$t0");
            emit_load_operand(output, frame, code->op2, "$t1");
            if(strcmp(code->relop, "==") == 0) {
                fprintf(output, "  beq $t0, $t1, %s\n", code->name);
            } else if(strcmp(code->relop, "!=") == 0) {
                fprintf(output, "  bne $t0, $t1, %s\n", code->name);
            } else if(strcmp(code->relop, ">") == 0) {
                fprintf(output, "  bgt $t0, $t1, %s\n", code->name);
            } else if(strcmp(code->relop, "<") == 0) {
                fprintf(output, "  blt $t0, $t1, %s\n", code->name);
            } else if(strcmp(code->relop, ">=") == 0) {
                fprintf(output, "  bge $t0, $t1, %s\n", code->name);
            } else {
                fprintf(output, "  ble $t0, $t1, %s\n", code->name);
            }
            continue;
        }
        if(code->kind == IR_RETURN) {
            emit_load_operand(output, frame, code->op1, "$v0");
            fprintf(output, "  lw $ra, %d($fp)\n", ra_offset);
            fprintf(output, "  lw $fp, %d($fp)\n", fp_offset);
            fprintf(output, "  addi $sp, $sp, %d\n", frame->frame_size);
            fprintf(output, "  jr $ra\n");
            continue;
        }
        if(code->kind == IR_DEC) {
            continue;
        }
        if(code->kind == IR_ARG) {
            emit_load_operand(output, frame, code->op1, "$t0");
            fprintf(output, "  addi $sp, $sp, -4\n");
            fprintf(output, "  sw $t0, 0($sp)\n");
            ++pending_args;
            continue;
        }
        if(code->kind == IR_CALL) {
            fprintf(output, "  jal %s\n", code->name);
            if(pending_args > 0) {
                fprintf(output, "  addi $sp, $sp, %d\n", pending_args * 4);
                pending_args = 0;
            }
            emit_store_result(output, frame, code->result, "$v0", "$t0");
            continue;
        }
        if(code->kind == IR_READ) {
            fprintf(output, "  jal read\n");
            emit_store_result(output, frame, code->result, "$v0", "$t0");
            continue;
        }
        if(code->kind == IR_WRITE) {
            emit_load_operand(output, frame, code->op1, "$a0");
            fprintf(output, "  jal write\n");
        }
    }
}

void mips_generate(FILE *output, const IRModule *module) {
    int i;
    if(output == NULL || module == NULL) {
        return;
    }
    emit_read_write_helpers(output);
    for(i = 0; i < module->count; ++i) {
        FrameInfo frame;
        frame = build_frame(&module->functions[i]);
        emit_function(output, &module->functions[i], &frame);
        free_frame(&frame);
    }
}
