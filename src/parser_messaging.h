#ifndef PARSER_MESSAGING_H
#define PARSER_MESSAGING_H

#include "parser.h"
#include "parser_types.h"
#include "parser_di.h" // Mandatory include to inherit DILifetime and HandlerDependency

// Specific nodes for Messaging and CQRS in the AST
typedef enum {
    NODE_MSG_COMMAND = 400,
    NODE_MSG_QUERY,
    NODE_MSG_NOTIFICATION,
    NODE_MSG_HANDLER,
    NODE_STRUCT_PROP
} MessagingASTNodeType;

// Structure for properties of commands, queries, and notifications
typedef struct MessageProperty {
    char* prop_name;
    TypeInfo* prop_type;
    bool is_required;
    bool is_mutable;
} MessageProperty;

// Main AST node for Messaging and CQRS integrated with arena-based DI management
typedef struct MessagingASTNode {
    int type;
    char* name;
    union {
        // Data for command and notification
        struct {
            MessageProperty* properties;
            int property_count;
        } msg_struct;

        // Data for query (has an associated return type)
        struct {
            MessageProperty* properties;
            int property_count;
            TypeInfo* return_intent_type;
        } query_struct;

        // Data for: handler for Name { ... } with dependency allocation scopes
        struct {
            bool is_async_handler;
            char* handle_param_name;
            TypeInfo* handle_return_type;
            HandlerDependency* dependencies; // Inherited from parser_di.h to link arenas
            int dependency_count;
        } msg_handler;
    } data;
} MessagingASTNode;

// Public signatures for the Messaging parser
MessagingASTNode* parse_messaging_structure(Parser* parser);
MessagingASTNode* parse_messaging_handler(Parser* parser);
void free_messaging_ast_node(MessagingASTNode* node);

#endif // PARSER_MESSAGING_H