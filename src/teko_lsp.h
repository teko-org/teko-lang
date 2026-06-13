#ifndef TEKO_LSP_H
#define TEKO_LSP_H

#include <stdbool.h>
#include "project_manager.h"

// Request types standardized by the LSP protocol
typedef enum {
    LSP_REQ_INITIALIZE,      // Maps the initial handshake from the IDE
    LSP_REQ_DID_OPEN,        // Notifies that the user opened a .tks file
    LSP_REQ_DID_CHANGE,      // Notifies real-time text changes (for the linter)
    LSP_REQ_FORMATTING,      // Triggered by the format code shortcut (fmt)
    LSP_REQ_COMPLETION,      // Triggered for method autocompletion (e.g. @marshall.)
    LSP_REQ_UNKNOWN
} LSPRequestKind;

// LSP Server structure (tekols)
typedef struct {
    bool is_initialized;
    char* current_workspace_root;
    TekoProjectConfig* active_project;
} TekoLanguageServer;

// Public signatures of the Language Server
TekoLanguageServer* teko_lsp_create(void);
LSPRequestKind teko_lsp_parse_request(const char* json_rpc_payload);
void teko_lsp_process_formatting(TekoLanguageServer* server, const char* file_path);
void teko_lsp_send_diagnostics(TekoLanguageServer* server, const char* file_path, const char* error_msg, int line);
void teko_lsp_destroy(TekoLanguageServer* server);

#endif // TEKO_LSP_H