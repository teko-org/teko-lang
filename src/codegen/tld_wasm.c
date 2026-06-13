#include "tld_wasm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// LEB128 compression algorithm for unsigned 32-bit integers
uint32_t tld_wasm_encode_leb128(uint32_t value, uint8_t* out_buffer) {
    uint32_t bytes_written = 0;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80; // Sets the most significant bit (MSB) if more bytes are coming
        }
        if (out_buffer) {
            out_buffer[bytes_written] = byte;
        }
        bytes_written++;
    } while (value != 0);
    return bytes_written;
}

// Helper to inject structured sections into the .wasm binary file
static void tld_wasm_write_section(FILE* out, TekoWasmSectionId sec_id, const uint8_t* sec_data, uint32_t sec_size) {
    uint8_t leb_buf[5];

    // Writes the Section ID (1 byte)
    uint8_t id_byte = (uint8_t)sec_id;
    fwrite(&id_byte, 1, 1, out);

    // Writes the section size compressed in LEB128
    uint32_t leb_bytes = tld_wasm_encode_leb128(sec_size, leb_buf);
    fwrite(leb_buf, 1, leb_bytes, out);

    // Writes the physical section data
    if (sec_size > 0 && sec_data) {
        fwrite(sec_data, 1, sec_size, out);
    }
}

bool tld_wasm_write_module(const char* filename, const uint8_t* code_payload, uint32_t payload_size) {
    if (!filename || !code_payload || payload_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    // 1. WRITES THE MANDATORY INITIAL HEADER (\x00asm\x01\x00\x00\x00)
    uint8_t header[] = {
        WASM_MAGIC_0, WASM_MAGIC_1, WASM_MAGIC_2, WASM_MAGIC_3,
        WASM_VERSION_0, WASM_VERSION_1, WASM_VERSION_2, WASM_VERSION_3
    };
    fwrite(header, 1, sizeof(header), out);

    // 2. SECTION 1: TYPE SECTION (Declares the function signature: () -> i32)
    uint8_t type_sec[] = {
        0x01,              // Number of types/signatures in the vector (1)
        WASM_TYPE_FUNC,    // Function signature type (0x60)
        0x00,              // Input parameter count (0)
        0x01,              // Output return count (1)
        WASM_TYPE_I32      // Return type: i32 (0x7F)
    };
    tld_wasm_write_section(out, WASM_SEC_TYPE, type_sec, sizeof(type_sec));

    // 3. SECTION 3: FUNCTION SECTION (Maps function 0 to the signature at index 0)
    uint8_t func_sec[] = {
        0x01,              // Function count (1)
        0x00               // Corresponding type index (0)
    };
    tld_wasm_write_section(out, WASM_SEC_FUNCTION, func_sec, sizeof(func_sec));

    // 4. SECTION 5: MEMORY SECTION (Allocates stable O(1) linear memory of 1 page of 64KB)
    uint8_t mem_sec[] = {
        0x01,              // Memory count (1)
        0x00,              // Flags: minimum limit only (0)
        0x01               // Initial size: 1 page (64KB) for Arenas
    };
    tld_wasm_write_section(out, WASM_SEC_MEMORY, mem_sec, sizeof(mem_sec));

    // 5. SECTION 7: EXPORT SECTION (Exports the $main function under the public label "main")
    uint8_t export_sec[] = {
        0x01,              // Export count (1)
        0x04, 'm', 'a', 'i', 'n', // Exported text name: "main" (4 bytes + string)
        0x00,              // Export type: Function (0)
        0x00               // Exported function index (0)
    };
    tld_wasm_write_section(out, WASM_SEC_EXPORT, export_sec, sizeof(export_sec));

    // 6. SECTION 10: CODE SECTION (Injects the raw instructions from the metal compiler)
    // The Code layout requires: [Total_Size] [Function_Count] [Body_Size] [Locals_Vector] [Payload_Instructions] [0x0B_End]
    uint32_t body_size = 1 + payload_size + 1; // 1 byte locals vector + instructions + 1 byte op_end (0x0B)
    uint8_t* code_sec_buf = (uint8_t*)malloc(body_size + 5);

    if (code_sec_buf) {
        uint32_t cursor = 0;
        cursor += tld_wasm_encode_leb128(1, &code_sec_buf[cursor]); // Function count (1)
        cursor += tld_wasm_encode_leb128(body_size, &code_sec_buf[cursor]); // Body size

        code_sec_buf[cursor++] = 0x00; // Additional local variables vector (0 entries)
        memcpy(&code_sec_buf[cursor], code_payload, payload_size);
        cursor += payload_size;
        code_sec_buf[cursor++] = 0x0B; // Mandatory formal scope terminator: end [INDEX]

        tld_wasm_write_section(out, WASM_SEC_CODE, code_sec_buf, cursor);
        free(code_sec_buf);
    }

    fclose(out);
    return true;
}
