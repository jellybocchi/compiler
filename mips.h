#ifndef MIPS_H
#define MIPS_H

#include <stdio.h>
#include "ircode.h"

void mips_generate(FILE *output, const IRModule *module);

#endif
