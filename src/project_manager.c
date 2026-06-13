#include "project_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char* trim_quotes(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len - 1] == '"') {
        char* new_str = malloc(len - 1);
        if (new_str) {
            strncpy(new_str, str + 1, len - 2);
            new_str[len - 2] = '\0';
            return new_str;
        }
    }
    return strdup(str);
}

TekoProjectConfig* teko_project_load(const char* tkp_filepath) {
    if (!tkp_filepath) return NULL;

    FILE* file = fopen(tkp_filepath, "r");
    if (!file) {
        fprintf(stderr, "[Project Error]: Could not open manifest '%s'.\n", tkp_filepath);
        return NULL;
    }

    auto config = (TekoProjectConfig*)malloc(sizeof(TekoProjectConfig));
    if (!config) {
        fclose(file);
        return NULL;
    }
    config->project_name = NULL;
    config->version = NULL;
    config->author = NULL;
    config->root_dir = NULL;
    config->target_type = TARGET_UNKNOWN;

    char* path_copy = strdup(tkp_filepath);
    char* last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        config->root_dir = strdup(path_copy);
    } else {
        config->root_dir = strdup(".");
    }
    free(path_copy);

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "project ") && strchr(line, '{')) {
            char name_buf[128];
            if (sscanf(line, "project %127s", name_buf) == 1) {
                char* brace = strchr(name_buf, '{');
                if (brace) *brace = '\0';
                config->project_name = strdup(name_buf);
            }
        }
        else if (strstr(line, "version:")) {
            char val_buf[128];
            if (sscanf(line, " version: %127[^;]", val_buf) == 1) {
                config->version = trim_quotes(val_buf);
            }
        }
        else if (strstr(line, "author:")) {
            char val_buf[128];
            if (sscanf(line, " author: %127[^;]", val_buf) == 1) {
                config->author = trim_quotes(val_buf);
            }
        }
        else if (strstr(line, "type:")) { // <-- NEW: Capture the output type from the manifest
            char val_buf[128];
            if (sscanf(line, " type: %127[^;]", val_buf) == 1) {
                char* clean_val = trim_quotes(val_buf);
                if (strcmp(clean_val, "executable") == 0)     config->target_type = TARGET_EXECUTABLE;
                else if (strcmp(clean_val, "static_lib") == 0)  config->target_type = TARGET_STATIC_LIB;
                else if (strcmp(clean_val, "dynamic_lib") == 0) config->target_type = TARGET_DYNAMIC_LIB;
                free(clean_val);
            }
        }
    }

    fclose(file);
    return config;
}

bool teko_project_validate_structure(const TekoProjectConfig* config) {
    if (!config || !config->root_dir) return false;

    // RULE 1: If the manifest omits the type or it is unknown, break the build
    if (config->target_type == TARGET_UNKNOWN) {
        fprintf(stderr, "[Configuration Error]: The target type (type) was not defined in the .tkp manifest of project '%s'.\n", config->project_name);
        return false;
    }

    // RULE 2: The physical check for main.tks becomes conditional, required only for executables
    if (config->target_type == TARGET_EXECUTABLE) {
        char main_path[512];
        snprintf(main_path, sizeof(main_path), "%s/main.tks", config->root_dir);
        if (access(main_path, F_OK) != 0) {
            fprintf(stderr, "[Structure Error]: Projects of type 'executable' require the entry point 'main.tks'.\n");
            return false;
        }
    }

    return true;
}

void teko_project_free(TekoProjectConfig* config) {
    if (!config) return;
    if (config->project_name) free(config->project_name);
    if (config->version) free(config->version);
    if (config->author) free(config->author);
    if (config->root_dir) free(config->root_dir);
    free(config);
}