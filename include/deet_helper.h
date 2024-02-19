#ifndef DEET_HELPER_H
#define DEET_HELPER_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <string.h>
#include <signal.h>

#include "deet.h"

typedef struct{
    int deet_id;
    pid_t process_id;
    // T or U
    char traced;
    // NONE, RUNNING, STOPPING, STOPPED, CONTINUING, KILLED, DEAD
    PSTATE state;
    // you could also do strlen(argv)/8 to get length
    char *argv_str;
    int return_value;
}process;

extern process* deet_processes;
extern int deet_processes_size;
extern volatile int max_deet_processes;
//extern volatile int terminal;
//extern volatile int current_deet_processes;
//extern int sig_chld_flag;

//pid_t Fork();

//void Ptrace_child();

int first_word(char* line);

char** argv_builder(int* argc, char * cmd_line);

int str_to_int(char* ptr);

int print_process_fast(int deet_id);

void print_process_printf(int deet_id);

int print_process(int deet_id);

int print_processes();

int add_process(pid_t pid, char* argv_str);

//void set_process_state(int deet_id, char* status);

void sigint_handler(int signum);

void chld_handler(int signum);

int chld_seen();

void send_signal(int signal, int deet_id);

int is_last_arg_deet_id(char* last_part);

void wait_for_deetid(int deet_id, char* status);

void wait_for_all_dead();

void strip_end(char* str);

int state_to_str(char* buffer, process this_process);
#endif