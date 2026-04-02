#include "optimizer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char **items;
    int count;
    int capacity;
} NameSet;

typedef struct {
    char *name;
    IROperand value;
} ValueEntry;

typedef struct {
    ValueEntry *entries;
    int count;
    int capacity;
} ValueMap;

typedef struct {
    IRCodeKind kind;
    IROperand op1;
    IROperand op2;
    IROperand result;
} ExprEntry;

typedef struct {
    ExprEntry *entries;
    int count;
    int capacity;
} ExprTable;

typedef struct {
    int start;
    int end;
    int succ[2];
    int succ_count;
    NameSet use;
    NameSet def;
    NameSet live_in;
    NameSet live_out;
} BasicBlock;

typedef struct {
    int invariant_count;
    IRCode *invariants;
} LoopInfo;

static char *optimizer_strdup(const char *source) {
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

static void nameset_add(NameSet *set, const char *name) {
    char **items;
    int i;

    if(name == NULL) {
        return;
    }

    for(i = 0; i < set->count; ++i) {
        if(strcmp(set->items[i], name) == 0) {
            return;
        }
    }

    if(set->count == set->capacity) {
        set->capacity = set->capacity == 0 ? 8 : set->capacity * 2;
        items = (char **)realloc(
            set->items,
            (size_t)set->capacity * sizeof(char *)
        );
        if(items == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(1);
        }
        set->items = items;
    }

    set->items[set->count] = optimizer_strdup(name);
    ++set->count;
}

static int nameset_contains(const NameSet *set, const char *name) {
    int i;

    if(name == NULL) {
        return 0;
    }

    for(i = 0; i < set->count; ++i) {
        if(strcmp(set->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void nameset_remove(NameSet *set, const char *name) {
    int i;

    if(name == NULL) {
        return;
    }

    for(i = 0; i < set->count; ++i) {
        if(strcmp(set->items[i], name) == 0) {
            free(set->items[i]);
            set->items[i] = set->items[set->count - 1];
            --set->count;
            return;
        }
    }
}

static void nameset_union(NameSet *dest, const NameSet *src) {
    int i;

    for(i = 0; i < src->count; ++i) {
        nameset_add(dest, src->items[i]);
    }
}

static int nameset_equal(const NameSet *left, const NameSet *right) {
    int i;

    if(left->count != right->count) {
        return 0;
    }

    for(i = 0; i < left->count; ++i) {
        if(!nameset_contains(right, left->items[i])) {
            return 0;
        }
    }
    return 1;
}

static void nameset_copy(NameSet *dest, const NameSet *src) {
    int i;

    for(i = 0; i < src->count; ++i) {
        nameset_add(dest, src->items[i]);
    }
}

static void nameset_clear(NameSet *set) {
    int i;

    for(i = 0; i < set->count; ++i) {
        free(set->items[i]);
    }
    free(set->items);
    memset(set, 0, sizeof(NameSet));
}

static void valuemap_set(ValueMap *map, const char *name, IROperand value) {
    ValueEntry *entries;
    int i;

    if(name == NULL) {
        return;
    }

    for(i = 0; i < map->count; ++i) {
        if(strcmp(map->entries[i].name, name) == 0) {
            ircode_free_operand(&map->entries[i].value);
            map->entries[i].value = ircode_copy_operand(value);
            return;
        }
    }

    if(map->count == map->capacity) {
        map->capacity = map->capacity == 0 ? 8 : map->capacity * 2;
        entries = (ValueEntry *)realloc(
            map->entries,
            (size_t)map->capacity * sizeof(ValueEntry)
        );
        if(entries == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(1);
        }
        map->entries = entries;
    }

    map->entries[map->count].name = optimizer_strdup(name);
    map->entries[map->count].value = ircode_copy_operand(value);
    ++map->count;
}

static void valuemap_clear_name(ValueMap *map, const char *name) {
    int i;

    if(name == NULL) {
        return;
    }

    for(i = 0; i < map->count; ++i) {
        if(strcmp(map->entries[i].name, name) == 0) {
            free(map->entries[i].name);
            ircode_free_operand(&map->entries[i].value);
            map->entries[i] = map->entries[map->count - 1];
            --map->count;
            return;
        }
    }
}

static IROperand valuemap_get(const ValueMap *map, const char *name) {
    int i;

    for(i = 0; i < map->count; ++i) {
        if(strcmp(map->entries[i].name, name) == 0) {
            return ircode_copy_operand(map->entries[i].value);
        }
    }

    return ircode_make_none();
}

static void valuemap_clear(ValueMap *map) {
    int i;

    for(i = 0; i < map->count; ++i) {
        free(map->entries[i].name);
        ircode_free_operand(&map->entries[i].value);
    }
    free(map->entries);
    memset(map, 0, sizeof(ValueMap));
}

static void exprtable_add(
    ExprTable *table,
    IRCodeKind kind,
    IROperand op1,
    IROperand op2,
    IROperand result
) {
    ExprEntry *entries;

    if(table->count == table->capacity) {
        table->capacity = table->capacity == 0 ? 8 : table->capacity * 2;
        entries = (ExprEntry *)realloc(
            table->entries,
            (size_t)table->capacity * sizeof(ExprEntry)
        );
        if(entries == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(1);
        }
        table->entries = entries;
    }

    table->entries[table->count].kind = kind;
    table->entries[table->count].op1 = ircode_copy_operand(op1);
    table->entries[table->count].op2 = ircode_copy_operand(op2);
    table->entries[table->count].result = ircode_copy_operand(result);
    ++table->count;
}

static IROperand exprtable_find(const ExprTable *table,IRCodeKind kind,IROperand op1,IROperand op2) {
    int i;
    for(i = 0; i < table->count; ++i) {
        if(
            table->entries[i].kind == kind &&
            ircode_operand_equal(table->entries[i].op1, op1) &&
            ircode_operand_equal(table->entries[i].op2, op2)
        ) {
            return ircode_copy_operand(table->entries[i].result);
        }
    }

    return ircode_make_none();
}

static void exprtable_invalidate_name(ExprTable *table, const char *name) {
    int i;
    int j;
    if(name == NULL) {
        return;
    }
    i = 0;
    while(i < table->count) {
        int match;
        match = 0;
        if(table->entries[i].op1.name != NULL) {
            if(strcmp(table->entries[i].op1.name, name) == 0) {
                match = 1;
            }
        }
        if(table->entries[i].op2.name != NULL) {
            if(strcmp(table->entries[i].op2.name, name) == 0) {
                match = 1;
            }
        }
        if(table->entries[i].result.name != NULL) {
            if(strcmp(table->entries[i].result.name, name) == 0) {
                match = 1;
            }
        }
        if(match) {
            ircode_free_operand(&table->entries[i].op1);
            ircode_free_operand(&table->entries[i].op2);
            ircode_free_operand(&table->entries[i].result);
            for(j = i + 1; j < table->count; ++j) {
                table->entries[j - 1] = table->entries[j];
            }
            --table->count;
        } else {
            ++i;
        }
    }
}

static void exprtable_clear(ExprTable *table) {
    int i;

    for(i = 0; i < table->count; ++i) {
        ircode_free_operand(&table->entries[i].op1);
        ircode_free_operand(&table->entries[i].op2);
        ircode_free_operand(&table->entries[i].result);
    }
    free(table->entries);
    memset(table, 0, sizeof(ExprTable));
}

static int is_commutative(IRCodeKind kind) {
    return kind == IR_ADD || kind == IR_MUL;
}

static int operand_compare(IROperand left, IROperand right) {
    char left_buffer[64];
    char right_buffer[64];
    const char *left_text;
    const char *right_text;

    if(left.kind == IR_OPERAND_CONSTANT) {
        snprintf(left_buffer, sizeof(left_buffer), "#%d", left.value);
        left_text = left_buffer;
    } else if(left.name != NULL) {
        left_text = left.name;
    } else {
        left_text = "";
    }

    if(right.kind == IR_OPERAND_CONSTANT) {
        snprintf(right_buffer, sizeof(right_buffer), "#%d", right.value);
        right_text = right_buffer;
    } else if(right.name != NULL) {
        right_text = right.name;
    } else {
        right_text = "";
    }

    return strcmp(left_text, right_text);
}

static void normalize_expr(IRCode *code) {
    if(!is_commutative(code->kind)) {
        return;
    }

    if(operand_compare(code->op1, code->op2) > 0) {
        IROperand temp;

        temp = code->op1;
        code->op1 = code->op2;
        code->op2 = temp;
    }
}

static IROperand substitute_operand(const ValueMap *map, IROperand operand) {
    IROperand value;

    if(operand.kind != IR_OPERAND_VARIABLE) {
        return operand;
    }

    value = valuemap_get(map, operand.name);
    if(value.kind == IR_OPERAND_NONE) {
        return operand;
    }

    ircode_free_operand(&operand);
    return value;
}

static int fold_binary(IRCode *code) {
    int left_value;
    int right_value;
    int result;

    if(
        code->op1.kind == IR_OPERAND_CONSTANT &&
        code->op2.kind == IR_OPERAND_CONSTANT
    ) {
        left_value = code->op1.value;
        right_value = code->op2.value;

        if(code->kind == IR_ADD) {
            result = left_value + right_value;
        } else if(code->kind == IR_SUB) {
            result = left_value - right_value;
        } else if(code->kind == IR_MUL) {
            result = left_value * right_value;
        } else {
            if(right_value == 0) {
                return 0;
            }
            result = left_value / right_value;
        }

        ircode_free_operand(&code->op1);
        ircode_free_operand(&code->op2);
        code->kind = IR_ASSIGN;
        code->op1 = ircode_make_constant(result);
        return 1;
    }

    if(code->kind == IR_ADD) {
        if(code->op1.kind == IR_OPERAND_CONSTANT && code->op1.value == 0) {
            ircode_free_operand(&code->op1);
            code->kind = IR_ASSIGN;
            code->op1 = code->op2;
            code->op2 = ircode_make_none();
            return 1;
        }
        if(code->op2.kind == IR_OPERAND_CONSTANT && code->op2.value == 0) {
            ircode_free_operand(&code->op2);
            code->kind = IR_ASSIGN;
            return 1;
        }
    }

    if(code->kind == IR_SUB && code->op2.kind == IR_OPERAND_CONSTANT && code->op2.value == 0) {
        ircode_free_operand(&code->op2);
        code->kind = IR_ASSIGN;
        return 1;
    }

    if(code->kind == IR_MUL) {
        if(code->op1.kind == IR_OPERAND_CONSTANT && code->op1.value == 1) {
            ircode_free_operand(&code->op1);
            code->kind = IR_ASSIGN;
            code->op1 = code->op2;
            code->op2 = ircode_make_none();
            return 1;
        }
        if(code->op2.kind == IR_OPERAND_CONSTANT && code->op2.value == 1) {
            ircode_free_operand(&code->op2);
            code->kind = IR_ASSIGN;
            return 1;
        }
        if(code->op1.kind == IR_OPERAND_CONSTANT && code->op1.value == 0) {
            ircode_free_operand(&code->op2);
            code->kind = IR_ASSIGN;
            return 1;
        }
        if(code->op2.kind == IR_OPERAND_CONSTANT && code->op2.value == 0) {
            ircode_free_operand(&code->op2);
            ircode_free_operand(&code->op1);
            code->kind = IR_ASSIGN;
            code->op1 = ircode_make_constant(0);
            return 1;
        }
    }

    if(code->kind == IR_DIV && code->op2.kind == IR_OPERAND_CONSTANT && code->op2.value == 1) {
        ircode_free_operand(&code->op2);
        code->kind = IR_ASSIGN;
        return 1;
    }

    return 0;
}

static void optimize_basic_block(IRCode *codes, int start, int end) {
    ValueMap values;
    ExprTable exprs;
    int i;

    memset(&values, 0, sizeof(ValueMap));
    memset(&exprs, 0, sizeof(ExprTable));

    for(i = start; i <= end; ++i) {
        IRCode *code;
        IROperand existing;

        code = &codes[i];

        if(code->kind == IR_ASSIGN) {
            code->op1 = substitute_operand(&values, code->op1);
            if(code->result.kind == IR_OPERAND_VARIABLE) {
                valuemap_clear_name(&values, code->result.name);
                exprtable_invalidate_name(&exprs, code->result.name);
                if(
                    code->op1.kind == IR_OPERAND_CONSTANT ||
                    code->op1.kind == IR_OPERAND_VARIABLE
                ) {
                    valuemap_set(&values, code->result.name, code->op1);
                }
            } else {
                valuemap_clear(&values);
                exprtable_clear(&exprs);
            }
            continue;
        }

        if(
            code->kind == IR_ADD ||
            code->kind == IR_SUB ||
            code->kind == IR_MUL ||
            code->kind == IR_DIV
        ) {
            code->op1 = substitute_operand(&values, code->op1);
            code->op2 = substitute_operand(&values, code->op2);
            normalize_expr(code);
            fold_binary(code);
            if(code->kind == IR_ASSIGN) {
                --i;
                continue;
            }

            existing = exprtable_find(&exprs, code->kind, code->op1, code->op2);
            if(existing.kind != IR_OPERAND_NONE) {
                ircode_free_operand(&code->op1);
                ircode_free_operand(&code->op2);
                code->kind = IR_ASSIGN;
                code->op1 = existing;
                code->op2 = ircode_make_none();
            } else {
                exprtable_add(&exprs, code->kind, code->op1, code->op2, code->result);
            }

            valuemap_clear_name(&values, code->result.name);
            exprtable_invalidate_name(&exprs, code->result.name);
            continue;
        }

        if(code->kind == IR_IF_GOTO) {
            code->op1 = substitute_operand(&values, code->op1);
            code->op2 = substitute_operand(&values, code->op2);
            continue;
        }

        if(code->kind == IR_RETURN || code->kind == IR_ARG || code->kind == IR_WRITE) {
            code->op1 = substitute_operand(&values, code->op1);
            continue;
        }

        if(code->kind == IR_READ || code->kind == IR_CALL) {
            valuemap_clear_name(&values, code->result.name);
            exprtable_invalidate_name(&exprs, code->result.name);
            continue;
        }

        if(code->kind == IR_PARAM || code->kind == IR_DEC) {
            valuemap_clear_name(&values, code->result.name);
            exprtable_invalidate_name(&exprs, code->result.name);
            continue;
        }

        if(code->result.kind == IR_OPERAND_DEREF) {
            valuemap_clear(&values);
            exprtable_clear(&exprs);
            memset(&values, 0, sizeof(ValueMap));
            memset(&exprs, 0, sizeof(ExprTable));
        }
    }

    valuemap_clear(&values);
    exprtable_clear(&exprs);
}

static void simplify_control_flow(IRFunction *function) {
    int i;

    for(i = 0; i < function->count; ++i) {
        IRCode *code;
        int left_value;
        int right_value;
        int condition;

        code = &function->codes[i];
        if(code->kind == IR_IF_GOTO) {
            if(
                code->op1.kind == IR_OPERAND_CONSTANT &&
                code->op2.kind == IR_OPERAND_CONSTANT
            ) {
                left_value = code->op1.value;
                right_value = code->op2.value;
                condition = 0;

                if(strcmp(code->relop, "==") == 0) {
                    condition = left_value == right_value;
                } else if(strcmp(code->relop, "!=") == 0) {
                    condition = left_value != right_value;
                } else if(strcmp(code->relop, ">") == 0) {
                    condition = left_value > right_value;
                } else if(strcmp(code->relop, "<") == 0) {
                    condition = left_value < right_value;
                } else if(strcmp(code->relop, ">=") == 0) {
                    condition = left_value >= right_value;
                } else if(strcmp(code->relop, "<=") == 0) {
                    condition = left_value <= right_value;
                }

                if(condition) {
                    ircode_free_operand(&code->op1);
                    ircode_free_operand(&code->op2);
                    free(code->relop);
                    code->relop = NULL;
                    code->kind = IR_GOTO;
                } else {
                    ircode_free_code(code);
                    code->kind = IR_NOP;
                }
            }
        }
    }
}

static void remove_unreachable(IRFunction *function) {
    int reachable;
    int i;

    reachable = 1;
    for(i = 0; i < function->count; ++i) {
        IRCode *code;

        code = &function->codes[i];
        if(code->kind == IR_LABEL) {
            reachable = 1;
            continue;
        }
        if(!reachable) {
            ircode_free_code(code);
            code->kind = IR_NOP;
            continue;
        }
        if(code->kind == IR_GOTO || code->kind == IR_RETURN) {
            reachable = 0;
        }
    }
}

static void compact_function(IRFunction *function) {
    int i;
    int write_index;

    write_index = 0;
    for(i = 0; i < function->count; ++i) {
        if(function->codes[i].kind == IR_NOP) {
            ircode_free_code(&function->codes[i]);
            continue;
        }

        if(write_index != i) {
            function->codes[write_index] = function->codes[i];
        }
        ++write_index;
    }
    function->count = write_index;
}

static int is_temp_name(const char *name) {
    return name != NULL && name[0] == 't';
}

static int find_label_index(const IRFunction *function, const char *label) {
    int i;

    if(label == NULL) {
        return -1;
    }

    for(i = 0; i < function->count; ++i) {
        if(function->codes[i].kind == IR_LABEL) {
            if(strcmp(function->codes[i].name, label) == 0) {
                return i;
            }
        }
    }
    return -1;
}

static int redirect_jumps(
    IRFunction *function,
    const char *from,
    const char *to
) {
    int i;
    int changed;

    changed = 0;

    for(i = 0; i < function->count; ++i) {
        IRCode *code;

        code = &function->codes[i];
        if(code->kind == IR_GOTO || code->kind == IR_IF_GOTO) {
            if(code->name != NULL && strcmp(code->name, from) == 0) {
                free(code->name);
                code->name = optimizer_strdup(to);
                changed = 1;
            }
        }
    }

    return changed;
}

static void simplify_empty_blocks(IRFunction *function) {
    int i;
    int changed;

    do {
        changed = 0;
        for(i = 0; i + 1 < function->count; ++i) {
            IRCode *code;
            IRCode *next;

            code = &function->codes[i];
            next = &function->codes[i + 1];
            if(code->kind == IR_LABEL && next->kind == IR_GOTO) {
                if(
                    code->name != NULL &&
                    next->name != NULL &&
                    strcmp(code->name, next->name) != 0
                ) {
                    if(redirect_jumps(function, code->name, next->name)) {
                        changed = 1;
                    }
                }
            }
        }
    } while(changed);
}

static void remove_unused_labels(IRFunction *function) {
    int i;

    for(i = 0; i < function->count; ++i) {
        IRCode *code;
        int used;
        int j;

        code = &function->codes[i];
        if(code->kind != IR_LABEL) {
            continue;
        }

        used = 0;
        for(j = 0; j < function->count; ++j) {
            if(function->codes[j].kind == IR_GOTO || function->codes[j].kind == IR_IF_GOTO) {
                if(
                    function->codes[j].name != NULL &&
                    strcmp(function->codes[j].name, code->name) == 0
                ) {
                    used = 1;
                    break;
                }
            }
        }

        if(!used) {
            ircode_free_code(code);
            code->kind = IR_NOP;
        }
    }
}

static void collect_uses(IRCode *code, NameSet *uses) {
    if(code->op1.kind == IR_OPERAND_VARIABLE || code->op1.kind == IR_OPERAND_DEREF) {
        nameset_add(uses, code->op1.name);
    }
    if(code->op2.kind == IR_OPERAND_VARIABLE || code->op2.kind == IR_OPERAND_DEREF) {
        nameset_add(uses, code->op2.name);
    }
    if(code->result.kind == IR_OPERAND_DEREF) {
        nameset_add(uses, code->result.name);
    }
}

static const char *code_def_name(const IRCode *code) {
    if(code->result.kind == IR_OPERAND_VARIABLE) {
        return code->result.name;
    }
    return NULL;
}

static int code_is_removable(const IRCode *code) {
    if(code->kind == IR_ASSIGN) {
        if(code->result.kind == IR_OPERAND_VARIABLE) {
            return 1;
        }
    }
    if(
        code->kind == IR_ADD ||
        code->kind == IR_SUB ||
        code->kind == IR_MUL ||
        code->kind == IR_DIV
    ) {
        return code->result.kind == IR_OPERAND_VARIABLE;
    }
    return 0;
}

static int find_label_block(
    const IRFunction *function,
    BasicBlock *blocks,
    int block_count,
    const char *label
) {
    int i;

    for(i = 0; i < block_count; ++i) {
        if(function->codes[blocks[i].start].kind == IR_LABEL) {
            if(strcmp(function->codes[blocks[i].start].name, label) == 0) {
                return i;
            }
        }
    }
    return -1;
}

static BasicBlock *build_blocks(IRFunction *function, int *block_count_out) {
    int *leaders;
    BasicBlock *blocks;
    int block_count;
    int i;
    int j;

    leaders = (int *)calloc((size_t)function->count, sizeof(int));
    if(leaders == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    block_count = 0;
    if(function->count > 0) {
        leaders[0] = 1;
    }

    for(i = 0; i < function->count; ++i) {
        IRCode *code;

        code = &function->codes[i];
        if(code->kind == IR_LABEL) {
            leaders[i] = 1;
        }
        if(
            (code->kind == IR_GOTO || code->kind == IR_IF_GOTO || code->kind == IR_RETURN) &&
            i + 1 < function->count
        ) {
            leaders[i + 1] = 1;
        }
    }

    for(i = 0; i < function->count; ++i) {
        if(leaders[i]) {
            ++block_count;
        }
    }

    blocks = (BasicBlock *)calloc((size_t)block_count, sizeof(BasicBlock));
    if(blocks == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    j = -1;
    for(i = 0; i < function->count; ++i) {
        if(leaders[i]) {
            ++j;
            blocks[j].start = i;
            if(j > 0) {
                blocks[j - 1].end = i - 1;
            }
        }
    }
    if(block_count > 0) {
        blocks[block_count - 1].end = function->count - 1;
    }

    for(i = 0; i < block_count; ++i) {
        IRCode *last;
        int k;

        last = &function->codes[blocks[i].end];
        if(last->kind == IR_GOTO) {
            j = find_label_block(function, blocks, block_count, last->name);
            if(j >= 0) {
                blocks[i].succ[0] = j;
                blocks[i].succ_count = 1;
            }
        } else if(last->kind == IR_IF_GOTO) {
            j = find_label_block(function, blocks, block_count, last->name);
            if(j >= 0) {
                blocks[i].succ[blocks[i].succ_count] = j;
                ++blocks[i].succ_count;
            }
            if(i + 1 < block_count) {
                blocks[i].succ[blocks[i].succ_count] = i + 1;
                ++blocks[i].succ_count;
            }
        } else if(last->kind != IR_RETURN && i + 1 < block_count) {
            blocks[i].succ[0] = i + 1;
            blocks[i].succ_count = 1;
        }

        for(j = blocks[i].start; j <= blocks[i].end; ++j) {
            NameSet uses;
            const char *def_name;

            memset(&uses, 0, sizeof(NameSet));
            collect_uses(&function->codes[j], &uses);
            def_name = code_def_name(&function->codes[j]);

            for(k = 0; k < uses.count; ++k) {
                if(!nameset_contains(&blocks[i].def, uses.items[k])) {
                    nameset_add(&blocks[i].use, uses.items[k]);
                }
            }
            if(def_name != NULL) {
                nameset_add(&blocks[i].def, def_name);
            }
            nameset_clear(&uses);
        }
    }

    free(leaders);
    *block_count_out = block_count;
    return blocks;
}

static void free_blocks(BasicBlock *blocks, int block_count) {
    int i;

    for(i = 0; i < block_count; ++i) {
        nameset_clear(&blocks[i].use);
        nameset_clear(&blocks[i].def);
        nameset_clear(&blocks[i].live_in);
        nameset_clear(&blocks[i].live_out);
    }
    free(blocks);
}

static void run_dead_code_elimination(IRFunction *function) {
    BasicBlock *blocks;
    int block_count;
    int changed;
    int i;

    blocks = build_blocks(function, &block_count);

    do {
        changed = 0;
        for(i = block_count - 1; i >= 0; --i) {
            NameSet new_live_out;
            NameSet new_live_in;
            int j;

            memset(&new_live_out, 0, sizeof(NameSet));
            memset(&new_live_in, 0, sizeof(NameSet));

            for(j = 0; j < blocks[i].succ_count; ++j) {
                nameset_union(&new_live_out, &blocks[blocks[i].succ[j]].live_in);
            }

            nameset_copy(&new_live_in, &blocks[i].live_out);
            nameset_clear(&new_live_in);
            nameset_copy(&new_live_in, &new_live_out);
            for(j = 0; j < blocks[i].def.count; ++j) {
                nameset_remove(&new_live_in, blocks[i].def.items[j]);
            }
            nameset_union(&new_live_in, &blocks[i].use);

            if(
                !nameset_equal(&new_live_out, &blocks[i].live_out) ||
                !nameset_equal(&new_live_in, &blocks[i].live_in)
            ) {
                changed = 1;
                nameset_clear(&blocks[i].live_out);
                nameset_clear(&blocks[i].live_in);
                blocks[i].live_out = new_live_out;
                blocks[i].live_in = new_live_in;
            } else {
                nameset_clear(&new_live_out);
                nameset_clear(&new_live_in);
            }
        }
    } while(changed);

    for(i = 0; i < block_count; ++i) {
        NameSet live;
        int j;

        memset(&live, 0, sizeof(NameSet));
        nameset_copy(&live, &blocks[i].live_out);

        for(j = blocks[i].end; j >= blocks[i].start; --j) {
            IRCode *code;
            const char *def_name;
            NameSet uses;
            int k;

            code = &function->codes[j];
            memset(&uses, 0, sizeof(NameSet));
            collect_uses(code, &uses);
            def_name = code_def_name(code);

            if(code_is_removable(code) && def_name != NULL && !nameset_contains(&live, def_name)) {
                ircode_free_code(code);
                code->kind = IR_NOP;
            } else {
                if(def_name != NULL) {
                    nameset_remove(&live, def_name);
                }
                for(k = 0; k < uses.count; ++k) {
                    nameset_add(&live, uses.items[k]);
                }
            }
            nameset_clear(&uses);
        }
        nameset_clear(&live);
    }

    free_blocks(blocks, block_count);
}

static int operand_depends_on_loop(
    IROperand operand,
    const NameSet *defs,
    const NameSet *invariants
) {
    if(operand.kind != IR_OPERAND_VARIABLE) {
        return 0;
    }

    if(invariants != NULL && nameset_contains(invariants, operand.name)) {
        return 0;
    }

    return nameset_contains(defs, operand.name);
}

static void append_invariant_code(LoopInfo *loop, const IRCode *code) {
    IRCode *codes;

    codes = (IRCode *)realloc(
        loop->invariants,
        (size_t)(loop->invariant_count + 1) * sizeof(IRCode)
    );
    if(codes == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    loop->invariants = codes;
    loop->invariants[loop->invariant_count] = ircode_copy_code(code);
    ++loop->invariant_count;
}

static void free_loop_info(LoopInfo *loop) {
    int i;

    for(i = 0; i < loop->invariant_count; ++i) {
        ircode_free_code(&loop->invariants[i]);
    }
    free(loop->invariants);
    loop->invariants = NULL;
    loop->invariant_count = 0;
}

static int is_block_boundary(IRCodeKind kind) {
    if(kind == IR_LABEL) {
        return 1;
    }
    if(kind == IR_GOTO || kind == IR_IF_GOTO || kind == IR_RETURN) {
        return 1;
    }
    return 0;
}

static int range_has_boundary(
    const IRFunction *function,
    int start,
    int end
) {
    int i;

    for(i = start; i <= end; ++i) {
        if(is_block_boundary(function->codes[i].kind)) {
            return 1;
        }
    }

    return 0;
}

static int match_induction_update(
    const IRFunction *function,
    int index,
    const char **base_name_out,
    int *step_out
) {
    const IRCode *code;
    const IRCode *next;
    const char *base_name;
    int step;

    if(index + 1 >= function->count) {
        return 0;
    }

    code = &function->codes[index];
    next = &function->codes[index + 1];
    if(next->kind != IR_ASSIGN) {
        return 0;
    }
    if(
        code->kind != IR_ADD &&
        code->kind != IR_SUB
    ) {
        return 0;
    }
    if(
        code->result.kind != IR_OPERAND_VARIABLE ||
        next->result.kind != IR_OPERAND_VARIABLE ||
        next->op1.kind != IR_OPERAND_VARIABLE
    ) {
        return 0;
    }
    if(
        code->result.name == NULL ||
        next->op1.name == NULL ||
        strcmp(code->result.name, next->op1.name) != 0
    ) {
        return 0;
    }

    base_name = NULL;
    step = 0;
    if(
        code->op1.kind == IR_OPERAND_VARIABLE &&
        code->op2.kind == IR_OPERAND_CONSTANT
    ) {
        base_name = code->op1.name;
        if(code->kind == IR_ADD) {
            step = code->op2.value;
        } else {
            step = -code->op2.value;
        }
    } else if(
        code->kind == IR_ADD &&
        code->op2.kind == IR_OPERAND_VARIABLE &&
        code->op1.kind == IR_OPERAND_CONSTANT
    ) {
        base_name = code->op2.name;
        step = code->op1.value;
    }

    if(base_name == NULL) {
        return 0;
    }
    if(strcmp(next->result.name, base_name) != 0) {
        return 0;
    }

    *base_name_out = base_name;
    *step_out = step;
    return 1;
}

static int match_scaled_assignment(
    const IRFunction *function,
    int index,
    const char *base_name,
    const char **family_name_out,
    int *factor_out
) {
    const IRCode *code;
    const IRCode *next;
    int factor;

    if(index + 1 >= function->count) {
        return 0;
    }

    code = &function->codes[index];
    next = &function->codes[index + 1];
    if(code->kind != IR_MUL || next->kind != IR_ASSIGN) {
        return 0;
    }
    if(
        code->result.kind != IR_OPERAND_VARIABLE ||
        next->result.kind != IR_OPERAND_VARIABLE ||
        next->op1.kind != IR_OPERAND_VARIABLE
    ) {
        return 0;
    }
    if(
        code->result.name == NULL ||
        next->op1.name == NULL ||
        strcmp(code->result.name, next->op1.name) != 0
    ) {
        return 0;
    }

    if(
        code->op1.kind == IR_OPERAND_VARIABLE &&
        code->op1.name != NULL &&
        strcmp(code->op1.name, base_name) == 0 &&
        code->op2.kind == IR_OPERAND_CONSTANT
    ) {
        factor = code->op2.value;
    } else if(
        code->op2.kind == IR_OPERAND_VARIABLE &&
        code->op2.name != NULL &&
        strcmp(code->op2.name, base_name) == 0 &&
        code->op1.kind == IR_OPERAND_CONSTANT
    ) {
        factor = code->op1.value;
    } else {
        return 0;
    }

    *family_name_out = next->result.name;
    *factor_out = factor;
    return 1;
}

static int has_other_defs_in_range(
    const IRFunction *function,
    int start,
    int end,
    const char *name,
    int skip_a,
    int skip_b
) {
    int i;

    for(i = start; i <= end; ++i) {
        const char *def_name;

        if(i == skip_a || i == skip_b) {
            continue;
        }

        def_name = code_def_name(&function->codes[i]);
        if(def_name != NULL && strcmp(def_name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static int find_scaled_init(
    const IRFunction *function,
    int loop_start,
    const char *base_name,
    const char *family_name,
    int factor
) {
    int i;
    int found;

    found = -1;
    for(i = 0; i + 1 < loop_start; ++i) {
        const char *candidate_family;
        int candidate_factor;

        if(
            match_scaled_assignment(
                function,
                i,
                base_name,
                &candidate_family,
                &candidate_factor
            ) &&
            strcmp(candidate_family, family_name) == 0 &&
            candidate_factor == factor
        ) {
            found = i;
        }
    }

    if(found < 0) {
        return -1;
    }

    if(
        has_other_defs_in_range(
            function,
            found + 2,
            loop_start - 1,
            base_name,
            -1,
            -1
        )
    ) {
        return -1;
    }
    if(
        has_other_defs_in_range(
            function,
            found + 2,
            loop_start - 1,
            family_name,
            -1,
            -1
        )
    ) {
        return -1;
    }

    return found;
}

static void rewrite_scaled_update(
    IRCode *code,
    const char *family_name,
    int delta
) {
    ircode_free_operand(&code->op1);
    ircode_free_operand(&code->op2);

    if(delta == 0) {
        code->kind = IR_ASSIGN;
        code->op1 = ircode_make_variable(family_name);
        code->op2 = ircode_make_none();
        return;
    }

    if(delta > 0) {
        code->kind = IR_ADD;
        code->op1 = ircode_make_variable(family_name);
        code->op2 = ircode_make_constant(delta);
        return;
    }

    code->kind = IR_SUB;
    code->op1 = ircode_make_variable(family_name);
    code->op2 = ircode_make_constant(-delta);
}

static void run_strength_reduction(IRFunction *function) {
    int loop_end;

    for(loop_end = 0; loop_end < function->count; ++loop_end) {
        IRCode *back_edge;
        int loop_start;
        int update_index;

        back_edge = &function->codes[loop_end];
        if(back_edge->kind != IR_GOTO) {
            continue;
        }

        loop_start = find_label_index(function, back_edge->name);
        if(loop_start < 0 || loop_start >= loop_end) {
            continue;
        }

        for(update_index = loop_start + 1; update_index + 1 < loop_end; ++update_index) {
            const char *base_name;
            int step;
            int scaled_index;

            if(
                !match_induction_update(
                    function,
                    update_index,
                    &base_name,
                    &step
                )
            ) {
                continue;
            }

            if(
                has_other_defs_in_range(
                    function,
                    loop_start + 1,
                    loop_end - 1,
                    base_name,
                    update_index + 1,
                    -1
                )
            ) {
                continue;
            }

            for(
                scaled_index = update_index + 2;
                scaled_index + 1 < loop_end;
                ++scaled_index
            ) {
                const char *family_name;
                int factor;
                int delta;

                if(
                    range_has_boundary(
                        function,
                        update_index + 2,
                        scaled_index - 1
                    )
                ) {
                    break;
                }

                if(
                    !match_scaled_assignment(
                        function,
                        scaled_index,
                        base_name,
                        &family_name,
                        &factor
                    )
                ) {
                    continue;
                }

                if(
                    find_scaled_init(
                        function,
                        loop_start,
                        base_name,
                        family_name,
                        factor
                    ) < 0
                ) {
                    continue;
                }

                if(
                    has_other_defs_in_range(
                        function,
                        loop_start + 1,
                        loop_end - 1,
                        family_name,
                        scaled_index + 1,
                        -1
                    )
                ) {
                    continue;
                }

                delta = step * factor;
                rewrite_scaled_update(
                    &function->codes[scaled_index],
                    family_name,
                    delta
                );
            }
        }
    }
}

static void hoist_loop_invariants(IRFunction *function) {
    int i;

    for(i = 0; i < function->count; ++i) {
        IRCode *code;
        int loop_start;
        NameSet defs;
        NameSet invariant_names;
        LoopInfo loop;
        int j;

        code = &function->codes[i];
        if(code->kind != IR_GOTO) {
            continue;
        }

        loop_start = find_label_index(function, code->name);
        if(loop_start < 0 || loop_start >= i) {
            continue;
        }

        memset(&defs, 0, sizeof(NameSet));
        memset(&invariant_names, 0, sizeof(NameSet));
        memset(&loop, 0, sizeof(LoopInfo));

        for(j = loop_start + 1; j < i; ++j) {
            const char *def_name;

            def_name = code_def_name(&function->codes[j]);
            if(def_name != NULL) {
                nameset_add(&defs, def_name);
            }
        }

        for(j = loop_start + 1; j < i; ++j) {
            IRCode *current;
            const char *def_name;

            current = &function->codes[j];
            def_name = code_def_name(current);

            if(
                current->kind != IR_ASSIGN &&
                current->kind != IR_ADD &&
                current->kind != IR_SUB &&
                current->kind != IR_MUL &&
                current->kind != IR_DIV
            ) {
                continue;
            }

            if(def_name == NULL || !is_temp_name(def_name)) {
                continue;
            }

            if(
                operand_depends_on_loop(current->op1, &defs, &invariant_names) ||
                operand_depends_on_loop(current->op2, &defs, &invariant_names)
            ) {
                continue;
            }

            append_invariant_code(&loop, current);
            nameset_add(&invariant_names, def_name);
            ircode_free_code(current);
            current->kind = IR_NOP;
        }

        if(loop.invariant_count > 0) {
            IRCode *new_codes;
            int old_count;
            int insert_count;
            int write_index;

            compact_function(function);
            old_count = function->count;
            insert_count = loop.invariant_count;
            new_codes = (IRCode *)calloc(
                (size_t)(old_count + insert_count),
                sizeof(IRCode)
            );
            if(new_codes == NULL) {
                fprintf(stderr, "Out of memory.\n");
                exit(1);
            }

            write_index = 0;
            for(j = 0; j < old_count; ++j) {
                if(j == loop_start) {
                    int k;

                    for(k = 0; k < insert_count; ++k) {
                        new_codes[write_index] = loop.invariants[k];
                        ++write_index;
                    }
                    loop.invariants = NULL;
                    loop.invariant_count = 0;
                }
                new_codes[write_index] = function->codes[j];
                ++write_index;
            }

            free(function->codes);
            function->codes = new_codes;
            function->count = old_count + insert_count;
            function->capacity = old_count + insert_count;
        }

        nameset_clear(&defs);
        nameset_clear(&invariant_names);
        free_loop_info(&loop);
    }
}

void optimize_ir_module(IRModule *module) {
    int i;
    int changed;

    if(module == NULL) {
        return;
    }

    for(i = 0; i < module->count; ++i) {
        int j;
        int block_start;

        hoist_loop_invariants(&module->functions[i]);
        run_strength_reduction(&module->functions[i]);
        simplify_control_flow(&module->functions[i]);
        simplify_empty_blocks(&module->functions[i]);
        remove_unreachable(&module->functions[i]);
        compact_function(&module->functions[i]);

        block_start = 0;
        for(j = 0; j <= module->functions[i].count; ++j) {
            if(
                j == module->functions[i].count ||
                module->functions[i].codes[j].kind == IR_LABEL ||
                module->functions[i].codes[j].kind == IR_GOTO ||
                module->functions[i].codes[j].kind == IR_IF_GOTO ||
                module->functions[i].codes[j].kind == IR_RETURN
            ) {
                int block_end;

                block_end = j - 1;
                if(block_end >= block_start) {
                    optimize_basic_block(
                        module->functions[i].codes,
                        block_start,
                        block_end
                    );
                }
                block_start = j + 1;
            }
        }

        run_dead_code_elimination(&module->functions[i]);
        compact_function(&module->functions[i]);

        do {
            changed = 0;
            for(j = 0; j + 1 < module->functions[i].count; ++j) {
                IRCode *code;
                IRCode *next;

                code = &module->functions[i].codes[j];
                next = &module->functions[i].codes[j + 1];
                if(
                    code->kind == IR_GOTO &&
                    next->kind == IR_LABEL &&
                    strcmp(code->name, next->name) == 0
                ) {
                    ircode_free_code(code);
                    code->kind = IR_NOP;
                    changed = 1;
                } else if(
                    code->kind == IR_GOTO &&
                    next->kind == IR_GOTO
                ) {
                    ircode_free_code(next);
                    next->kind = IR_NOP;
                    changed = 1;
                }
            }
            if(changed) {
                compact_function(&module->functions[i]);
            }
        } while(changed);

        remove_unused_labels(&module->functions[i]);
        compact_function(&module->functions[i]);
        remove_unreachable(&module->functions[i]);
        compact_function(&module->functions[i]);
    }
}
