#include "debugger_cli.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static int debugger_trim_to_parent_dir(char* path)
{
    size_t len;

    if (!path || !path[0]) return 0;
    len = strlen(path);
    while (len > 0u && path[len - 1u] != '\\' && path[len - 1u] != '/') {
        len--;
    }
    while (len > 0u && (path[len - 1u] == '\\' || path[len - 1u] == '/')) {
        len--;
    }
    if (len == 0u) return 0;
    path[len] = '\0';
    return 1;
}

static void debugger_set_sdk_working_directory(void)
{
    char module_path[MAX_PATH];
    char build_path[MAX_PATH];
    DWORD build_attrs;

    if (GetModuleFileNameA(NULL, module_path, (DWORD)sizeof(module_path)) == 0u) return;
    if (!debugger_trim_to_parent_dir(module_path)) return; /* exe dir */
    if (!debugger_trim_to_parent_dir(module_path)) return; /* x64 dir */
    if (!debugger_trim_to_parent_dir(module_path)) return; /* SDK dir */
    build_path[0] = '\0';
    if (snprintf(build_path, sizeof(build_path), "%s\\Build", module_path) > 0) {
        build_attrs = GetFileAttributesA(build_path);
        if (build_attrs != INVALID_FILE_ATTRIBUTES &&
            (build_attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            SetCurrentDirectoryA(build_path);
            return;
        }
    }
    SetCurrentDirectoryA(module_path);
}

int main(int argc, char** argv)
{
    debugger_set_sdk_working_directory();

    if (argc < 2) {
        debugger_print_general_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "world") == 0) {
        return debugger_cmd_world(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "session") == 0) {
        return debugger_cmd_session(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "chunks") == 0) {
        return debugger_cmd_chunks(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "walls") == 0) {
        return debugger_cmd_walls(argc - 2, argv + 2);
    }

    if (argv[1][0] == '-') {
        return debugger_cmd_walls(argc - 1, argv + 1);
    }

    debugger_print_general_usage(argv[0]);
    return 1;
}
