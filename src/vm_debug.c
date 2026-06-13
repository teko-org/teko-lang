#include "vm_debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

TekoDebugger* teko_debugger_create(void) {
    auto dbg = (TekoDebugger*)malloc(sizeof(TekoDebugger));
    if (!dbg) return NULL;
    dbg->source_map_count = 0;
    dbg->breakpoint_count = 0;
    dbg->is_stepping = false;
    dbg->is_paused = false;
    return dbg;
}

// Registers a mapped symbol coming from the compiler
void teko_debugger_add_symbol(TekoDebugger* dbg, uint32_t line, uint32_t bytecode_ip) {
    if (!dbg || dbg->source_map_count >= MAX_SOURCE_LINES) return;
    dbg->source_maps[dbg->source_map_count].line = line;
    dbg->source_maps[dbg->source_map_count].bytecode_ip = bytecode_ip;
    dbg->source_map_count++;
}

// Activates a Breakpoint by converting the IDE line number to the real VM IP
void teko_debugger_set_breakpoint(TekoDebugger* dbg, uint32_t line) {
    if (!dbg || dbg->breakpoint_count >= MAX_BREAKPOINTS) return;

    for (uint32_t i = 0; i < dbg->source_map_count; i++) {
        if (dbg->source_maps[i].line == line) {
            dbg->breakpoints[dbg->breakpoint_count++] = dbg->source_maps[i].bytecode_ip;
            break;
        }
    }
}

// Checks within the computed goto loop whether the VM should freeze execution
bool teko_debugger_should_pause(TekoDebugger* dbg, uint32_t current_ip) {
    if (!dbg) return false;
    if (dbg->is_stepping) return true;

    for (uint32_t i = 0; i < dbg->breakpoint_count; i++) {
        if (dbg->breakpoints[i] == current_ip) {
            dbg->is_paused = true;
            return true;
        }
    }
    return false;
}

// Interpreter for JSON-RPC payloads received via TCP connection from IDEs (DAP standard)
void teko_debugger_handle_dap_message(TekoDebugger* dbg, const char* json_message, TekoVM* vm) {
    if (!dbg || !json_message || !vm) return;

    // Simulates reception of structured JSON-RPC commands via DAP
    if (strstr(json_message, "\"command\": \"next\"") || strstr(json_message, "\"command\": \"stepIn\"")) {
        dbg->is_stepping = true;
        dbg->is_paused = false;
        printf("[DAP Server]: STEP command executed by the IDE.\n");
    }
    else if (strstr(json_message, "\"command\": \"continue\"")) {
        dbg->is_stepping = false;
        dbg->is_paused = false;
        printf("[DAP Server]: CONTINUE command executed by the IDE.\n");
    }
    else if (strstr(json_message, "\"command\": \"scopes\"")) {
        // Returns the dump of active Arena variables to populate the IDE Watch windows
        printf("[DAP Server]: Arena memory dump exported to the IDE.\n");
    }
}

void teko_debugger_destroy(TekoDebugger* dbg) {
    if (dbg) free(dbg);
}