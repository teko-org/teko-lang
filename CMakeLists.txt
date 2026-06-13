cmake_minimum_required(VERSION 3.25)
project(teko C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Native Dependency Manager (Unity) ---
include(FetchContent)
FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity
        GIT_TAG        v2.6.1
)
FetchContent_MakeAvailable(unity)

# 1. Collect ALL .c and .h files from src and from ANY subfolder (such as src/codegen)
file(GLOB_RECURSE CORE_SOURCES "src/*.c" "src/*.h")

# Strictly remove the main entry point so it becomes the core static library
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

# Create the internal static library with the entire compiler engine
add_library(teko_core STATIC ${CORE_SOURCES})

# Ensure visibility of includes in relative subdirectories
target_include_directories(teko_core PRIVATE src)

# 2. TARGET 1: The Official Compiler (Production Entry Point)
add_executable(teko src/main.c)
target_link_libraries(teko PRIVATE teko_core)
target_include_directories(teko PRIVATE src)

# 3. TARGET 2: The Isolated Test Suite (Test Entry Point)
file(GLOB_RECURSE TEST_SOURCES "tests/*.c" "tests/*.h")
add_executable(teko_tests ${TEST_SOURCES})
target_link_libraries(teko_tests PRIVATE teko_core unity)
target_include_directories(teko_tests PRIVATE src)
