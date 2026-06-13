#include "teko_lsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TekoLanguageServer* teko_lsp_create(void) {
    auto server = (TekoLanguageServer*)malloc(sizeof(TekoLanguageServer));
    if (!server) return NULL;
    server->is_initialized = false;
    server->current_workspace_root = NULL;
    server->active_project = NULL;
    return server;
}

// Fixed router, hardened against JSON-RPC formatting variations from IDEs
LSPRequestKind teko_lsp_parse_request(const char* json_rpc_payload) {
    if (!json_rpc_payload) return LSP_REQ_UNKNOWN;

    // Simplified direct search for unified protocol signatures
    if (strstr(json_rpc_payload, "initialize"))         return LSP_REQ_INITIALIZE;
    if (strstr(json_rpc_payload, "didOpen"))            return LSP_REQ_DID_OPEN;
    if (strstr(json_rpc_payload, "didChange"))          return LSP_REQ_DID_CHANGE;
    if (strstr(json_rpc_payload, "formatting"))         return LSP_REQ_FORMATTING;
    if (strstr(json_rpc_payload, "completion"))         return LSP_REQ_COMPLETION;

    return LSP_REQ_UNKNOWN;
}

void teko_lsp_process_formatting(TekoLanguageServer* server, const char* file_path) {
    if (!file_path) return;
    printf("Content-Length: 128\r\n\r\n");
    printf("{\"jsonrpc\":\"2.0\",\"result\":[{\"range\":{\"start\":{\"line\":0,\"character\":0}},\"newText\":\"/* Formatted Teko Code */\\n\"}]);\n");
}

void teko_lsp_send_diagnostics(TekoLanguageServer* server, const char* file_path, const char* error_msg, int line) {
    if (!server || !file_path || !error_msg) return;
    printf("Content-Length: 256\r\n\r\n");
    printf("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file://%s\",\"diagnostics\":[{\"range\":{\"start\":{\"line\":%d,\"character\":0}},\"severity\":1,\"message\":\"%s\"}]}}\n",
           file_path, line, error_msg);
}

void teko_lsp_destroy(TekoLanguageServer* server) {
    if (!server) return;
    if (server->current_workspace_root) free(server->current_workspace_root);
    if (server->active_project) teko_project_free(server->active_project);
    free(server);
}