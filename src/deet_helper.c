#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <unistd.h>


#include "deet.h"
#include "deet_helper.h"


#define STARTING_ARR_SIZE 8

// array that stores all processes
process *deet_processes = NULL;
int deet_processes_size = STARTING_ARR_SIZE;
volatile int max_deet_processes = 0;
volatile int current_deet_processes = 0;

// int ** brkpnts = NULL; // grows with deet processes
// int * current_brkpnt = NULL; // which breakpoint each process is one
// int * ttl_breakpnts = NULL; // total breakpoints in each process

void wait_for_all_dead(){
    sigset_t open_mask;
    char* question_mark = "?\n";
    if (sigemptyset(&open_mask) == -1) {
        perror("sigemptyset");
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
        return;
    }

    while (current_deet_processes != 0){
        sigsuspend(&open_mask);
    }
}


// returns relative start at the beginnign of first word
char* strip_start(char *str){
    // blank space characters
    while(*str!=0 && (*str==' ' || *str=='\t' || *str=='\n')){
        str++;
    }
    return str;
}

// puts null terminator at the last consecutive "space" character from the right end of the string
void strip_end(char * str){
    // first character (character before null terminator)
    char * str_end = str + strlen(str) - 1;
    int max_characters = strlen(str);
    while(max_characters && (*str_end=='\n' || *str_end==' ' || *str_end=='\t')){
        *str_end-- = 0;
        max_characters--;
    }
}

// splits string into two parts: the first word, and the second part.
// Returns index of the start of the next word.
// if there is no next word, -1 is returned.
int first_word(char* line){
    line = strip_start(line);
    int i;
    for(i = 0; line[i]!=0 && line[i]!=' ' && line[i]!='\n' && line[i]!='\t'; i++);

    if(line[i] == 0){
        return -1;
    }

    line[i] = 0;
    return i+1;
}


// deconstructs a string into argv form, returning the start of the array.
// MUTATES cmd_line
// I do not think blocking signals is necessary here?
char** argv_builder(int* argc, char * cmd_line){
    int current_size = 8;
    char **argv = (char **)malloc(sizeof(char *) * current_size);
    int i = 0;
    int str_index = 0;
    // every word besides the last word
    while((str_index = first_word(cmd_line)) > 0){
        *(argv + i) = cmd_line;
        cmd_line = cmd_line + str_index;
        i += 1;
        if(i == current_size){
            //reallocate **argv
            if(current_size < 256){
                current_size *= 2;
            }else{
                current_size += 128;
            }
            argv = (char **)realloc(argv, current_size * sizeof(char *));
        }
    }
    // get the last word
    *(argv + i) = cmd_line;
    cmd_line = cmd_line + str_index;
    i += 1;
    // need one more space for a null terminator
    if(i == current_size){
        argv = (char **)realloc(argv, current_size + 1);
    }

    *(argv+i) = 0;
    *argc = i;
    return argv;
}


int str_to_int(char* ptr){
    int num = 0;
    while(*ptr != 0){
        if (*ptr < '0' || *ptr >'9'){
            return -1;
        }
        num = (num * 10) + (*ptr++ - '0');
        //int deet_id = atoi(part2);
        //print_process(num);
    }

    return num;
}


int state_to_str(char* buffer, process this_process){
    switch(this_process.state){
        case PSTATE_NONE:
            strcpy(buffer, "none");
            return 4;
        case PSTATE_RUNNING:
            strcpy(buffer, "running");
            return 7;
        case PSTATE_STOPPING:
            strcpy(buffer, "stopping");
            return 8;
        case PSTATE_STOPPED:
            strcpy(buffer, "stopped");
            return 7;
        case PSTATE_CONTINUING:
            strcpy(buffer, "continuing");
            return 10;
        case PSTATE_KILLED:
            strcpy(buffer, "killed");
            return 6;
        case PSTATE_DEAD:
            strcpy(buffer, "dead");
            return 4;
    }
    return -1;
}


// writes a decimal number to standard output file
void write_number(int number_to_write){
    int num_digits = 0;
    int buffer[21];
    if(number_to_write == 0){
        write(STDOUT_FILENO, "0", 1);
    }else{
        int number_copy = number_to_write;
        while (number_copy > 0){
            if(num_digits > 20){
                break;
            }
            buffer[num_digits] = number_copy % 10;
            number_copy /= 10;
            num_digits++;
        }
    }

    while(num_digits > 0){
        char digit = '0' + (buffer[num_digits-1]);
        write(STDOUT_FILENO, &digit, 1);
        num_digits--;
    }
}


// writes a hex value to standard output file
void write_hex(int number_to_write){
    write(STDOUT_FILENO, "0x", 2);
    int num_digits = 0;
    int buffer[21];
    if(number_to_write == 0){
        write(STDOUT_FILENO, "0", 1);
    }else{
        int number_copy = number_to_write;
        while (number_copy > 0){
            if(num_digits > 20){
                break;
            }
            buffer[num_digits] = number_copy % 16;
            number_copy /= 16;
            num_digits++;
        }
    }

    while(num_digits > 0){
        int this_val = buffer[num_digits-1];
        char digit;
        if(this_val <= 9){
            digit = '0' + (buffer[num_digits-1]);
        }else{
            digit = 'A' + (buffer[num_digits-1]-10);
        }
        write(STDOUT_FILENO, &digit, 1);
        num_digits--;
    }
}


// writes a number to a buffer given the number representation as an int
int write_num_to_buffer(char *buffer, int buffer_index, int number_to_write){
    int num_digits;
    int mini_buffer[21];
    // deet id
    if(number_to_write == 0){
        buffer[buffer_index] = '0';
        buffer_index++;
    }else{
        int number_copy = number_to_write;
        num_digits = 0;
        while (number_copy > 0){
            if(num_digits > 20){
                break;
            }
            mini_buffer[num_digits] = number_copy % 10;
            number_copy /= 10;
            num_digits++;
        }
        while(num_digits > 0){
            char digit = '0' + (mini_buffer[num_digits-1]);
            buffer[buffer_index] = digit;
            buffer_index += 1;
            num_digits--;
        }
    }

    return buffer_index;
}


// writes a hex number to a buffer given the decimal representation as an int
int write_hex_to_buffer(char* buffer, int buffer_index, int number_to_write){
    int num_digits = 0;
    int mini_buffer[21];

    buffer[buffer_index] = '0';
    buffer[buffer_index+1] = 'x';
    buffer_index += 2;
    if(number_to_write == 0){
        buffer[buffer_index] = '0';
        buffer_index++;
    }else{
        int number_copy = number_to_write;
        num_digits = 0;
        while (number_copy > 0){
            if(num_digits > 20){
                break;
            }
            mini_buffer[num_digits] = number_copy % 16;
            number_copy /= 16;
            num_digits++;
        }
        while(num_digits > 0){
            int this_val = mini_buffer[num_digits-1];
            char digit;
            if(this_val <= 9){
                digit = '0' + (mini_buffer[num_digits-1]);
            }else{
                digit = 'A' + (mini_buffer[num_digits-1]-10);
            }
            buffer[buffer_index] = digit;
            buffer_index += 1;
            num_digits--;
        }
    }

    return buffer_index;
}


// used to print a single process with deet id. Is signal safe.
int print_process(int deet_id){
    // verify deet_id is valid
    if(deet_id >= max_deet_processes || deet_processes[deet_id].state == PSTATE_NONE){
        return -1;
    }

    char p_state[11];
    int length = state_to_str(p_state, deet_processes[deet_id]);
    if(length == -1){
        return -1;
    }

    // write deet id
    if(deet_id == 0){
        write(STDOUT_FILENO, "0", 1);
    }else{
        write_number(deet_id);
    }
    write(STDOUT_FILENO, "\t", 1);

    // write pid
    if(deet_processes[deet_id].process_id == 0){
        write(STDOUT_FILENO, "0", 1);
    }else{
        write_number(deet_processes[deet_id].process_id);
    }
    write(STDOUT_FILENO, "\t", 1);

    // traced status
    write(STDOUT_FILENO, &(deet_processes[deet_id].traced), 1);
    write(STDOUT_FILENO, "\t", 1);

    // process state
    write(STDOUT_FILENO, p_state, length);
    write(STDOUT_FILENO, "\t", 1);

    // exit code
    if(deet_processes[deet_id].state == PSTATE_DEAD){
        // non zero return value
        if(deet_processes[deet_id].return_value){
            write_hex(deet_processes[deet_id].return_value);
        }else{ // zero return value
            write(STDOUT_FILENO, "0x0", 3);
        }
    }
    write(STDOUT_FILENO, "\t", 1);

    // what the process is executing
    char *argv_str_ptr = deet_processes[deet_id].argv_str;
    write(STDOUT_FILENO, argv_str_ptr, strlen(argv_str_ptr));

    write(STDOUT_FILENO, "\n", 1);

    return 0;
}


// uses a buffer to print faster. Prints the same content as print_process. Is signal safe.
int print_process_fast(int deet_id){
    if(deet_id >= max_deet_processes || deet_processes[deet_id].state == PSTATE_NONE){
        return -1;
    }

    char buffer[60];
    int buffer_index = 0;

    char p_state[11];
    int length = state_to_str(p_state, deet_processes[deet_id]);
    if(length == -1){
        return -1;
    }

    process deet_process = deet_processes[deet_id];

    //deet_id
    buffer_index = write_num_to_buffer(buffer, buffer_index, deet_id);
    buffer[buffer_index] = '\t';
    buffer_index += 1;
    
    // pid
    buffer_index = write_num_to_buffer(buffer, buffer_index, deet_process.process_id);
    buffer[buffer_index] = '\t';
    buffer_index += 1;
    
    // traced
    buffer[buffer_index] = deet_process.traced;
    buffer[buffer_index+1] = '\t';
    buffer_index += 2;

    // state
    for(int i=0; i<length; i++){
        buffer[buffer_index] = p_state[i];
        buffer_index++;
    }
    buffer[buffer_index] = '\t';
    buffer_index++;
    

    // write the exit code and execution str
    if(deet_processes[deet_id].state == PSTATE_DEAD){
        buffer_index = write_hex_to_buffer(buffer, buffer_index, deet_process.return_value);
    }
    buffer[buffer_index] = '\t';
    buffer_index++;

    // flush buffer
    write(STDOUT_FILENO, buffer, buffer_index);

    char *argv_str_ptr = deet_processes[deet_id].argv_str;
    int string_length = strlen(argv_str_ptr);
    argv_str_ptr[string_length] = '\n';
    // print argv and \n
    write(STDOUT_FILENO, argv_str_ptr, string_length + 1);
    // put back null terminator
    argv_str_ptr[string_length] = 0;

    return 1;
}


// prints the same content as the previous print process. Is not signal safe but is the fastest.
void print_process_printf(int deet_id){
    process deet_process = deet_processes[deet_id];
    char p_state[11];
    state_to_str(p_state, deet_processes[deet_id]);
    if(deet_process.state == PSTATE_DEAD){
        char * hex_header ="0x";
        printf("%d\t%d\t%c\t%s\t%s%X\t%s\n",
                deet_id,deet_process.process_id,deet_process.traced,p_state,
                hex_header,deet_process.return_value,deet_process.argv_str);
    }else{
        printf("%d\t%d\t%c\t%s\t\t%s\n",deet_id,deet_process.process_id,deet_process.traced,p_state,deet_process.argv_str);
    }
    fflush(stdout);
}


// prints processes
int print_processes(){
    sigset_t mask;
    if (sigemptyset(&mask) != 0) {
        perror("sigfiemptyset");
        return 1;
    }
    // Block SIGCHLD, (THIS PRINTS WE DONT WANT THAT!)
    if (sigaddset(&mask, SIGCHLD) != 0) {
        perror("sigaddset");
        return 1;
    }

    if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
        perror("sigprocmask");
        return 1;
    }

    for(int i=0; i<max_deet_processes; i++){
        process this_process = deet_processes[i];

        if(this_process.state == PSTATE_NONE){
            continue;
        }

        //print_process_fast(i);
        print_process_printf(i);
    }
    //fflush(stdout);

    // Unblock all signals
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0) {
        perror("sigprocmask (unblocking)");
        return 1;
    }

    return 0;
}


// returns deet_id assigned
// done by the child before exec
int add_process(pid_t pid, char* argv_str){
    if(deet_processes == NULL){
        deet_processes = (process *)malloc(STARTING_ARR_SIZE * sizeof(process));
    }

    // search for available pid
    int deet_id = max_deet_processes;
    for(int i=0; i<max_deet_processes; i++){
        if(deet_processes[i].state == PSTATE_DEAD || deet_processes[i].state == PSTATE_NONE){
            deet_id = i;
            break;
        }
    }

    for(int i=deet_id; i<max_deet_processes; i++){
        if(deet_processes[i].state == PSTATE_DEAD){
            deet_processes[i].state = PSTATE_NONE;
            // free strdupped memory
            free(deet_processes[i].argv_str);
        }
    }

    if(deet_id==max_deet_processes){
        if(deet_id >= deet_processes_size){
            // more memory is needed
            if(deet_processes_size < 256){
                deet_processes_size *= 2;
            }else{
                deet_processes_size += 128;
            }

            deet_processes = (process *)realloc(deet_processes, deet_processes_size * sizeof(process));
        }
        //deet_id += 1;
        max_deet_processes += 1;
    }
    
    current_deet_processes++;
    deet_processes[deet_id].deet_id = deet_id;
    deet_processes[deet_id].process_id = pid;
    deet_processes[deet_id].traced = 'T';
    deet_processes[deet_id].state = PSTATE_RUNNING;
    deet_processes[deet_id].argv_str = argv_str;
    deet_processes[deet_id].return_value = -1;
    
    return deet_id;
}


// input pid, deet_id comes out
int find_deet_id(pid_t pid){
    int deet_id = -1;
    for(int i=0; i<max_deet_processes; i++){
        if(deet_processes[i].process_id == pid){
            deet_id = i;
            break;
        }
    }

    return deet_id;
}


// chld_handler companion function
// change states of all pending children (does not set intermediate states: CONTINUING, STOPPING, KILLED)
int chld_seen(){
    // no child spotted!
    /*if(!sig_chld_flag){
        return 0;
    }

    sig_chld_flag = 0;*/

    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        int deet_id = find_deet_id(pid);

        if(deet_id == -1){
            char *string = "The PID did not exist for some reason. That's weird!\n";
            write(STDOUT_FILENO, string, strlen(string));
            continue;
        }

        if (WIFEXITED(status)) {
            current_deet_processes--;
            deet_processes[deet_id].state = PSTATE_DEAD;
            deet_processes[deet_id].return_value = WEXITSTATUS(status) << 8;
        } else if (WIFSIGNALED(status)){
            current_deet_processes--;
            deet_processes[deet_id].state = PSTATE_DEAD;
            deet_processes[deet_id].return_value = WTERMSIG(status);
        }else if (WIFSTOPPED(status)) {
            deet_processes[deet_id].state = PSTATE_STOPPED;
        } else if (WIFCONTINUED(status)) {
            deet_processes[deet_id].state = PSTATE_RUNNING;
        }
        //if(!terminal){
        print_process_fast(deet_id);
        //}
    }

    return 1;
}


//sigchld handler
void chld_handler(int signum){
    //sig_chld_flag = 1;
    chld_seen();
}


// sigint handler
void sigint_handler(int signum) {
    // Send SIGCHLD to itself (keep retrying until it works.)
    char *question_mark = "?\n";

    sigset_t mask;
    if (sigemptyset(&mask) != 0) {
        perror("sigemptyset");
        return;
    }
    if (sigaddset(&mask, SIGCHLD) != 0) {
        perror("sigaddset");
        return;
    }

    while (kill(getpid(), SIGCHLD) != 0) {
        perror("Error sending SIGCHLD");
        char* question_mark = "?\n";
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
    }

    // block sigchld signals
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
        return;
    }

    for(int i=0; i<max_deet_processes; i++){
        if(deet_processes[i].state != PSTATE_DEAD && deet_processes[i].state != PSTATE_NONE){
            deet_processes[i].state = PSTATE_KILLED;
            kill(deet_processes[i].process_id, SIGKILL);
        }
    }
    
    // Wait for all child processes to exit
    wait_for_all_dead();
    //while (wait(NULL) > 0);

    // unblock all signals
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
    }

    exit(0);
}


void send_signal(int signal, int deet_id){
    char* question_mark = "?\n";

    if (kill(deet_processes[deet_id].process_id, signal) == -1) {
        perror("kill");
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
    }
    
}


int is_last_arg_deet_id(char* last_part){
    int last_part_end = first_word(last_part);

    // too many arguments
    if (last_part_end != -1){
        // char *str = "peek: Input/output error. Too few arguments";
        // write(STDERR_FILENO, str, strlen(str));
        return -1;
    }

    // invalid deet id
    int deet_id = str_to_int(last_part);

    if(deet_id >= max_deet_processes){
        // write(STDOUT_FILENO, question_mark, strlen(question_mark));
        return -1;
    }

    return deet_id;
}


// waits for a deet process with deet id to reach a specific state
// requires a valid deet_id
// PSTATE status is the final state we want to be at upon termination
void wait_for_deetid(int deet_id, char* status){
    /*if(deet_processes[deet_id].state == PSTATE_NONE){
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
    }*/

    sigset_t mask;
    char* question_mark = "?\n";
    if (sigemptyset(&mask) == -1) {
        perror("sigemptyset");
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
        return;
    }

    PSTATE desired_state = PSTATE_DEAD;

    // continuing implies running
    if (!strcmp(status, "running") || !strcmp(status, "continuing")) {
        
        desired_state = PSTATE_RUNNING;
        
    }else if (!strcmp(status, "stopped") || !strcmp(status, "stopping")) {
        desired_state = PSTATE_STOPPED;
    }else if (!strcmp(status, "killed") || !strcmp(status, "dead")) {
        // do nothing
    }else{
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
        return;
    }

    while(deet_processes[deet_id].state != desired_state && deet_processes[deet_id].state != PSTATE_DEAD){
        sigsuspend(&mask);
    }
}

//