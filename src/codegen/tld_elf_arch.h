#ifndef TLD_ELF_ARCH_H
#define TLD_ELF_ARCH_H

#include <stdint.h>

#include "teko_target.h"
#include "../codegen_li.h"

// Returns the size in bytes that the instruction will generate on silicon
uint32_t tld_arch_encode_instruction(TekoArch arch, OpCode op, int32_t arg, uint8_t* out_buffer);

#endif // TLD_ELF_ARCH_H
