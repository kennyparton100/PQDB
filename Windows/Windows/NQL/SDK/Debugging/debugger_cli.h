#ifndef DEBUGGER_CLI_H
#define DEBUGGER_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

void debugger_print_general_usage(const char* program_name);
void debugger_print_world_usage(const char* program_name);
void debugger_print_session_usage(const char* program_name);
void debugger_print_chunks_usage(const char* program_name);
void debugger_print_walls_usage(const char* program_name);

int debugger_cmd_world(int argc, char** argv);
int debugger_cmd_session(int argc, char** argv);
int debugger_cmd_chunks(int argc, char** argv);
int debugger_cmd_walls(int argc, char** argv);
int debugger_cmd_session_play(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif
