#ifndef TLD_SYMBOLS_H
#define TLD_SYMBOLS_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SYMBOL_NAME 128
#define MAX_SYMBOL_COUNT 512

// Symbol type mapped on silicon
typedef enum {
    SYM_FUNC,      // Public function or method
    SYM_SERVICE,   // Service or native dependency injection
    SYM_RODATA     // Immutable string or constant (.rodata)
} TekoSymbolType;

// Physical structure of a Symbol in the Linker Table
typedef struct {
    char name[MAX_SYMBOL_NAME];
    TekoSymbolType type;
    uint64_t virtual_address; // Virtual address computed in RAM (VMA)
    uint32_t local_offset;     // Relative offset within its own byte block
    bool is_defined;          // True if the physical body was provided, False if it is an external dependency
} TekoLinkerSymbol;

// Structure of a Pending Reference (Where the dependency was invoked and needs injection)
typedef struct {
    char target_name[MAX_SYMBOL_NAME]; // Name of the symbol it is looking for
    uint32_t patch_offset;              // Exact position in the code where the jump bytes must be injected
    uint64_t call_site_vaddr;          // Virtual address from which the call is occurring
} TekoDependencyPatch;

// Unified Linker Table structure
typedef struct {
    TekoLinkerSymbol symbols[MAX_SYMBOL_COUNT];
    uint32_t symbol_count;
    TekoDependencyPatch patches[MAX_SYMBOL_COUNT];
    uint32_t patch_count;
} TekoSymbolTable;

// Public signatures of the Linker Dependency Manager
void tld_symbols_init(TekoSymbolTable* table);
bool tld_symbols_define(TekoSymbolTable* table, const char* name, TekoSymbolType type, uint32_t local_offset);
void tld_symbols_reference(TekoSymbolTable* table, const char* target_name, uint32_t patch_offset);
bool tld_symbols_resolve_and_inject(TekoSymbolTable* table, uint8_t* code_buffer, uint64_t base_vaddr);

#endif // TLD_SYMBOLS_H
