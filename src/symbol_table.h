#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdbool.h>
#include "parser_types.h"

typedef enum {
    SYM_VARIABLE,
    SYM_CONSTANT,
    SYM_FUNCTION
} SymbolKind;

// Structure representing an individual entry in the table
typedef struct Symbol {
    char* name;
    SymbolKind kind;
    TypeInfo* type_info;
    bool is_mutable;           // Controls whether declared with let (false) or mut (true)
    struct Symbol* next;       // Linked list for collision handling (chaining)
} Symbol;

// Structure representing a scope level (nested hierarchy)
typedef struct SymbolTableScope {
    Symbol** buckets;
    int size;
    struct SymbolTableScope* parent_scope; // Points to the parent scope (outer scope)
} SymbolTableScope;

// Public Symbol Table function signatures
SymbolTableScope* symbol_table_create_scope(SymbolTableScope* parent);
bool symbol_table_insert(SymbolTableScope* scope, const char* name, SymbolKind kind, TypeInfo* type, bool is_mutable);
Symbol* symbol_table_lookup(SymbolTableScope* scope, const char* name);
Symbol* symbol_table_lookup_current_scope(SymbolTableScope* scope, const char* name);
void symbol_table_free_scope(SymbolTableScope* scope);

#endif // SYMBOL_TABLE_H