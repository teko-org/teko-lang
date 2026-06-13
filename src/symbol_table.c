#include "symbol_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HASH_TABLE_INITIAL_SIZE 31

// Classic djb2 hash algorithm for fast string identifier mapping
static unsigned long hash_string(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

SymbolTableScope* symbol_table_create_scope(SymbolTableScope* parent) {
    SymbolTableScope* scope = (SymbolTableScope*)malloc(sizeof(SymbolTableScope));
    if (!scope) return NULL;

    scope->size = HASH_TABLE_INITIAL_SIZE;
    scope->parent_scope = parent;
    scope->buckets = (Symbol**)calloc(scope->size, sizeof(Symbol*));

    return scope;
}

bool symbol_table_insert(SymbolTableScope* scope, const char* name, SymbolKind kind, TypeInfo* type, bool is_mutable) {
    if (!scope || !name) return false;

    // Prevents direct redeclaration at the same local scope level
    if (symbol_table_lookup_current_scope(scope, name) != NULL) {
        fprintf(stderr, "[Semantic Error]: Symbol '%s' has already been declared in this scope.\n", name);
        return false;
    }

    unsigned long index = hash_string(name) % scope->size;
    Symbol* symbol = (Symbol*)malloc(sizeof(Symbol));
    if (!symbol) return false;

    symbol->name = strdup(name);
    symbol->kind = kind;
    symbol->type_info = type; // Binds the type metadata
    symbol->is_mutable = is_mutable;

    // Insert at the top of the corresponding bucket (left-chaining)
    symbol->next = scope->buckets[index];
    scope->buckets[index] = symbol;

    return true;
}

// Recursive lookup ascending the nested scope hierarchy (Block Scope -> Function -> Global)
Symbol* symbol_table_lookup(SymbolTableScope* scope, const char* name) {
    SymbolTableScope* current = scope;
    while (current != NULL) {
        Symbol* sym = symbol_table_lookup_current_scope(current, name);
        if (sym != NULL) return sym;
        current = current->parent_scope; // Move up one level
    }
    return NULL;
}

// Strict lookup restricted solely to the current local scope
Symbol* symbol_table_lookup_current_scope(SymbolTableScope* scope, const char* name) {
    if (!scope || !name) return NULL;

    unsigned long index = hash_string(name) % scope->size;
    Symbol* current = scope->buckets[index];

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void symbol_table_free_scope(SymbolTableScope* scope) {
    if (!scope) return;

    for (int i = 0; i < scope->size; i++) {
        Symbol* current = scope->buckets[i];
        while (current != NULL) {
            Symbol* temp = current;
            current = current->next;
            free(temp->name);
            // Note: type_info is not freed here as it belongs to the central AST
            free(temp);
        }
    }
    free(scope->buckets);
    free(scope);
}