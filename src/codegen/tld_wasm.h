#ifndef TLD_WASM_H
#define TLD_WASM_H

#include <stdint.h>
#include <stdbool.h>

// 4-byte magic signature and version of the WASM binary header
#define WASM_MAGIC_0        0x00
#define WASM_MAGIC_1        0x61
#define WASM_MAGIC_2        0x73
#define WASM_MAGIC_3        0x6D
#define WASM_VERSION_0      0x01
#define WASM_VERSION_1      0x00
#define WASM_VERSION_2      0x00
#define WASM_VERSION_3      0x00

// Official WASM Module Section Identifiers
typedef enum {
    WASM_SEC_CUSTOM   = 0,
    WASM_SEC_TYPE     = 1,  // Function signatures
    WASM_SEC_IMPORT   = 2,
    WASM_SEC_FUNCTION = 3,  // Function indices
    WASM_SEC_TABLE    = 4,
    WASM_SEC_MEMORY   = 5,  // Linear memory declaration
    WASM_SEC_GLOBAL   = 6,
    WASM_SEC_EXPORT   = 7,  // Exports of main/functions
    WASM_SEC_START    = 8,
    WASM_SEC_ELEMENT  = 9,
    WASM_SEC_CODE     = 10, // Binary instruction body (Opcodes)
    WASM_SEC_DATA     = 11  // Static strings and pooling (.rodata)
} TekoWasmSectionId;

// WebAssembly Primitive Data Types
#define WASM_TYPE_I32      0x7F
#define WASM_TYPE_FUNC     0x60

// Public signature of the Binary WASM Linker engine
uint32_t tld_wasm_encode_leb128(uint32_t value, uint8_t* out_buffer);
bool tld_wasm_write_module(const char* filename, const uint8_t* code_payload, uint32_t payload_size);

#endif // TLD_WASM_H
