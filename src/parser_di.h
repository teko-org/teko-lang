#ifndef PARSER_DI_H
#define PARSER_DI_H

#include "parser.h"
#include "parser_types.h"

// Specific nodes for the Dependency Injection architecture in the AST
typedef enum {
    NODE_DI_INTERFACE = 500,
    NODE_DI_SERVICE,
    NODE_DI_DECORATOR,
    NODE_INTERFACE_METHOD,
    NODE_METHOD_DECL
} DIASTNodeType;

// Lifetimes of natively injected arenas
typedef enum {
    LIFETIME_TRANSIENT, // New instance in the destination frame arena (default)
    LIFETIME_SCOPED,    // Fixed per green thread / request arena
    LIFETIME_SINGLETON  // Fixed in the main context arena (main)
} DILifetime;

// Structure for methods declared inside interfaces or implemented in services
typedef struct DIMethodSignature {
    char* method_name;
    TypeInfo* return_type;
    bool is_public;
    char* parameters_raw;
} DIMethodSignature;

// Structure for dependencies injected into handlers with arena binding
typedef struct HandlerDependency {
    char* dep_type;
    char* dep_name;
    DILifetime lifetime; // <-- NEW: Controls the dependency's allocation strategy
} HandlerDependency;

// AST node for the Dependency Injection ecosystem
typedef struct DIASTNode {
    int type;
    char* name;
    union {
        struct {
            bool is_exported;
            DIMethodSignature* methods;
            int method_count;
        } di_interface;

        struct {
            char* implements_interface;
            DIMethodSignature* methods;
            int method_count;
        } di_service;

        struct {
            char* next_param_name;
            char* target_interface;
            int precedence_order;
            DIMethodSignature* methods;
            int method_count;
        } di_decorator;

        // Handler properties with arena-directed injection
        struct {
            bool is_async_handler;
            char* handle_param_name;
            TypeInfo* handle_return_type;
            HandlerDependency* dependencies;
            int dependency_count;
        } msg_handler;
    } data;
} DIASTNode;

// Public signatures for the DI parser
DIASTNode* parse_di_interface(Parser* parser, bool is_exported);
DIASTNode* parse_di_service(Parser* parser);
DIASTNode* parse_di_decorator(Parser* parser);
void free_di_ast_node(DIASTNode* node);

#endif // PARSER_DI_H