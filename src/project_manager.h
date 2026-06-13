#ifndef PROJECT_MANAGER_H
#define PROJECT_MANAGER_H

#include <stdbool.h>

// Type of artifact the project will produce
typedef enum {
    TARGET_EXECUTABLE,
    TARGET_STATIC_LIB,
    TARGET_DYNAMIC_LIB,
    TARGET_UNKNOWN
} TekoTargetType;

// Updated metadata structure for the .tkp manifest
typedef struct {
    char* project_name;
    char* version;
    char* author;
    char* root_dir;
    TekoTargetType target_type;
} TekoProjectConfig;

// Public signatures of the Project Manager
TekoProjectConfig* teko_project_load(const char* tkp_filepath);
bool teko_project_validate_structure(const TekoProjectConfig* config);
void teko_project_free(TekoProjectConfig* config);

#endif // PROJECT_MANAGER_H