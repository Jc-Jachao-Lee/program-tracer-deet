#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/ptrace.h>
#include <unistd.h>

#include "deet_helper.h"
#include "deet.h"


int main(int argc, char *argv[]) {
    //int opt;
    int show_output = 1;
    char *cmd_line = NULL;
    size_t line_length = 0;
    size_t buffer_length = 0;
    pid_t pid = -1;

    

    const char* question_mark = "?\n";
    char *help_message = "Available commands:\n"
                    "help -- Print this help message\n"
                    "quit (<=0 args) -- Quit the program\n"
                    "show (<=1 args) -- Show process info\n"
                    "run (>=1 args) -- Start a process\n"
                    "stop (1 args) -- Stop a running process\n"
                    "cont (1 args) -- Continue a stopped process\n"
                    "release (1 args) -- Stop tracing a process, allowing it to continue normally\n"
                    "wait (1-2 args) -- Wait for a process to enter a specified state\n"
                    "kill (1 args) -- Forcibly terminate a process\n"
                    "peek (2-3 args) -- Read from the address space of a traced process\n"
                    "poke (3 args) -- Write to the address space of a traced process\n"
                    "bt (1 args) -- Show a stack trace for a traced process\n";
    
    for(int i=1; i < argc; i++){
        if(strlen(argv[i]) != 2){
            continue;
        }

        if(strcmp(argv[i], "-p") == 0){
            show_output = 0;
        }
    }

    // sig int signal handler
    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("Error installing SIGINT handler");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa_chld;
    sa_chld.sa_handler = chld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = 0;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("Error installing SIGCHLD handler");
        exit(EXIT_FAILURE);
    }

    // blocks sigchld
    sigset_t mask;

    if (sigemptyset(&mask) != 0) {
        perror("sigemptyset (Startup Failure)");
        exit(EXIT_FAILURE);
    }
    if (sigaddset(&mask, SIGCHLD) != 0) {
        perror("sigaddset (Startup Failure)");
        exit(EXIT_FAILURE);
    }

    int exit_loop = 0;
    while(!exit_loop){

        // block all signals
        
            if(show_output){
                char * msg = "deet> ";
                write(1, msg, strlen(msg));
            }
        //}

        clearerr(stdin);
        if((line_length = getline(&cmd_line, &buffer_length, stdin)) == -1){
            if (feof(stdin)){
                break;
            }else{
                if (errno == EINTR) {
                    errno = 0;
                    continue;
                } else {
                    perror("getline");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    errno = 0;
                    continue;
                }
                
            }
        }

        strip_end(cmd_line);
        
        
        int part1_end = first_word(cmd_line);
        char *word1 = cmd_line;
        char *part2 = cmd_line + part1_end;
        

        if (!strcmp(word1, "help")){
            write(STDOUT_FILENO, help_message, strlen(help_message));
            
        }else if (!strcmp(word1, "quit")){
            exit_loop = 1;

        }else if (!strcmp(word1, "show")){
            // there is no second part
            if(part1_end == -1){
                if(print_processes()){
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }
            }else{
                int deet_id = str_to_int(part2);
                if(deet_id == -1){
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }
                // verify deet_id is valid
                if(deet_id >= max_deet_processes || deet_processes[deet_id].state == PSTATE_NONE){
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }

                if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }
                
                print_process_printf(deet_id);

                if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }
            }

        }else if(!strcmp(word1, "run")){
            // no next word, nothing to run.
            if(part1_end == -1){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                fflush(stdout);
                continue;
            }

            // block sigchld signals
            if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            // use part 2 as argv (error process)
            
            // save the string, generate argv, add process to list
            int i = 0;
            char *part2_clone = strdup(part2);
            char **p_argv = argv_builder(&i, part2);
            // make sure process exists. We will fix pid later in parent.
            //int deet_id = add_process(-1, part2_clone);

            if((pid = fork()) == 0){ // child process

                if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    exit(EXIT_FAILURE);
                }

                // unblock all signals
                if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    exit(EXIT_FAILURE);
                }              

                //Ptrace_child();
                if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
                    perror("ptrace");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    exit(EXIT_FAILURE);
                }

                execvp(*p_argv, p_argv);

                 // if execvp return it means an error occured
                 exit(EXIT_FAILURE);

            }else if(pid < 0){ //error case
                perror("fork error");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }else{ // parent process

                // delays execution of code until child process execvp
                
                //wait_for_pid(pid, "stopped");
                //deet_processes[deet_id].process_id = pid;
                int deet_id = add_process(pid, part2_clone);
                //print_process(deet_id); // most certainly before it is stopped because sigchld is blocked here!

                print_process_printf(deet_id);

                // make sure process starts before cont
                wait_for_deetid(deet_id, "stopped");

                // unblock all signals
                if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }

                free(p_argv);
                
            }

        }else if (!strcmp(word1, "stop")){
            int deet_id;
            if((deet_id = is_last_arg_deet_id(part2)) == -1){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if(deet_processes[deet_id].state != PSTATE_RUNNING){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            // stopping -> stopped
            deet_processes[deet_id].state = PSTATE_STOPPING;
            //print_process(deet_id);
            print_process_printf(deet_id);

            if (kill(deet_processes[deet_id].process_id, SIGSTOP) == -1) {
                perror("kill");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
            }

        }else if (!strcmp(word1, "cont")){
            int deet_id;
            if((deet_id = is_last_arg_deet_id(part2)) == -1){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if(deet_processes[deet_id].state != PSTATE_STOPPED){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if(deet_processes[deet_id].traced == 'T'){

                if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                        perror("sigprocmask");
                        write(STDOUT_FILENO, question_mark, strlen(question_mark));
                        continue;
                }

                // Continue the execution of the traced child process
                if (ptrace(PTRACE_CONT, deet_processes[deet_id].process_id, NULL, NULL) == -1) {
                    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                        perror("sigprocmask");
                        write(STDOUT_FILENO, question_mark, strlen(question_mark));
                        continue;
                    }

                    perror("ptrace continue");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }

                // with signal blocks we can put this in else block
                deet_processes[deet_id].state = PSTATE_RUNNING;
                print_process_printf(deet_id);

                if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }
            }
            else{

                if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                }

                deet_processes[deet_id].state = PSTATE_CONTINUING;
                //print_process(deet_id);
                print_process_printf(deet_id);
                
                //send_signal(SIGCONT, deet_id);
                if (kill(deet_processes[deet_id].process_id, SIGCONT) == -1) {
                    perror("kill");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }

                if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                }
            }

        }else if (!strcmp(word1, "release")){
            int deet_id;
            if((deet_id = is_last_arg_deet_id(part2)) == -1){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            

            if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                    perror("sigprocmask");
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
            }

            if(deet_processes[deet_id].traced == 'U'){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if (ptrace(PTRACE_DETACH, deet_processes[deet_id].process_id, NULL, NULL) == -1) {
                perror("ptrace detach");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            
            deet_processes[deet_id].traced = 'U';

            // with signal blocks we can put this in else block
            deet_processes[deet_id].state = PSTATE_RUNNING;
            //print_process(deet_id); // signals are blocked so it wont be dead (dead won't be processed.)
            print_process_printf(deet_id);
                
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }


        }else if (!strcmp(word1, "wait")){
            int part2_end = first_word(part2);
            // if more than 1 arg we have to make sure theres only 1 arg and then set state to state desired.
            char * part3 = "dead";
            if(part2_end != -1){
                part3 = part2 + part2_end;
                int part3_end = first_word(part3);

                if(part3_end != -1){
                    write(1, question_mark, strlen(question_mark));
                }
            }

            int deet_id = str_to_int(part2);
            if(deet_id >= max_deet_processes || deet_id < 0){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            
            wait_for_deetid(str_to_int(part2), part3);

        }else if (!strcmp(word1, "kill")){
            int deet_id;
            if((deet_id = is_last_arg_deet_id(part2)) == -1){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if(deet_processes[deet_id].state == PSTATE_DEAD || 
               deet_processes[deet_id].state == PSTATE_NONE ||
               deet_processes[deet_id].state == PSTATE_KILLED ){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
            }

            deet_processes[deet_id].state = PSTATE_KILLED;
            //print_process(deet_id);
            print_process_printf(deet_id);

            //send_signal(SIGKILL, deet_id);
            if (kill(deet_processes[deet_id].process_id, SIGKILL) == -1) {
                perror("kill");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            //wait_for_deetid(deet_id, "dead");

            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
            }

        }else if (!strcmp(word1, "peek")){
            int part2_end = first_word(part2);
            if (part2_end == -1){
                //char *str = "peek: Input/output error. Too few arguments";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            char *part3 = part2 + part2_end;
            int part3_end = first_word(part3);
            

            int deet_id = str_to_int(part2);
            // error checking
            if(deet_id == -1){
                //char *str = "peek: Input/output error";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }else if(deet_id >= max_deet_processes || deet_processes[deet_id].traced != 'T'){
                //char *str = "show: Input/output error, invalid process";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }else if(deet_processes[deet_id].state != PSTATE_STOPPED){
                //char *str = "show: Input/output error, process is not stopped";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            pid_t pid = deet_processes[deet_id].process_id;

            struct user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, pid, 0, &regs) == -1) {
                perror("ptrace");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            /****************/
            // block signal chld (for print f)
            if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            // Convert hex string to long long int
            char *endptr;
            unsigned long int target_address = strtoll(part3, &endptr, 16);
            if (*endptr != '\0') {
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            //unsigned long int peeked_data;
            unsigned long int peeked_data = ptrace(PTRACE_PEEKDATA, pid, target_address, NULL);
            if (peeked_data == -1) {
                perror("ptrace");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            printf("%016lx\t%016lx\n", target_address, peeked_data);
            fflush(stdout);

            if(part3_end != -1){ // 3 arg peek
                char *part4 = part3 + part3_end;
                if (first_word(part4) != -1){
                    write(STDOUT_FILENO, question_mark, strlen(question_mark));
                    continue;
                }

                // extract number of words
                int number_of_word_to_read = str_to_int(part4);

                for(int i=1; i < number_of_word_to_read; i++){
                    unsigned long int next_addr = target_address + sizeof(unsigned long int) * i;

                    unsigned long int peeked_data = ptrace(PTRACE_PEEKDATA, pid, next_addr, NULL);
                    if (peeked_data == -1) {
                        perror("ptrace");
                        write(STDOUT_FILENO, question_mark, strlen(question_mark));
                        continue;
                    }
                    printf("%016lx\t%016lx\n", target_address, peeked_data);
                    fflush(stdout);
                }
            }

            // unblock all signals
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
            }
            /****************/

            

        }else if (!strcmp(word1, "poke")){
            int part2_end = first_word(part2);
            if (part2_end == -1){
                //char *str = "peek: Input/output error. Too few arguments";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            char *part3 = part2 + part2_end;
            int part3_end = first_word(part3);
            if (part3_end == -1){
                //char *str = "peek: Input/output error. Too few arguments";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            char *part4 = part3 + part3_end;
            if(first_word(part4) != -1){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            

            int deet_id = str_to_int(part2);
            // error checking
            if(deet_id == -1){
                //char *str = "peek: Input/output error";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }else if(deet_id >= max_deet_processes || deet_processes[deet_id].traced != 'T'){
                //char *str = "show: Input/output error, invalid process";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }else if(deet_processes[deet_id].state != PSTATE_STOPPED){
                //char *str = "show: Input/output error, process is not stopped";
                //write(STDERR_FILENO, str, strlen(str));
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            struct user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, pid, 0, &regs) == -1) {
                perror("ptrace");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }

            /****************/
            // block all signals (for print f) (besides sigint)
            if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
            }

            // Convert hex string to long long int
            char *endptr;
            unsigned long int target_address = strtoll(part3, &endptr, 16);
            if (*endptr != '\0') {
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            unsigned long int data_to_set = strtoll(part4, &endptr, 16);
            if (*endptr != '\0') {
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            
            long value = ptrace(PTRACE_POKEDATA, pid, target_address, data_to_set);
            if (value == -1) {
                perror("ptrace");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }


            // unblock all signals
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                perror("sigprocmask");
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
            }
            /****************/

        }else if (!strcmp(word1, "bt")){
            int part2_end = first_word(part2);
            int stack_trace_length = 10;
            // if more than 1 arg we have to make sure theres only 1 arg and then set state to state desired.
            if(part2_end != -1){
                char *part3 = part2 + part2_end;
                int part3_end = first_word(part3);

                if(part3_end != -1){
                    write(1, question_mark, strlen(question_mark));
                    continue;
                }
                // convert to int
                if((stack_trace_length = str_to_int(part3)) == -1){
                    write(1, question_mark, strlen(question_mark));
                    continue;
                }
            }

            int deet_id = str_to_int(part2);
            if(deet_id >= max_deet_processes){
                write(STDOUT_FILENO, question_mark, strlen(question_mark));
                continue;
            }
            
            pid_t pid = deet_processes[deet_id].process_id;
            struct user_regs_struct regs;

            // Block signal sigchld
            if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
                perror("sigprocmask");
                continue;
            }

            // Get the initial stack pointer
            if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) {
                perror("ptrace");
                continue;
            }

            unsigned long rbp = regs.rbp;

            // Iterate through the stack frames
            for (int i = 0; i < stack_trace_length; ++i) {

                // Read the word from the stack
                long word = ptrace(PTRACE_PEEKDATA, pid, rbp, NULL);
                long return_val = ptrace(PTRACE_PEEKDATA, pid, rbp + 0x8, NULL);
                if(return_val == 0){
                    break;
                }else if(return_val == -1){
                    perror("ptrace");
                    write(1, question_mark, strlen(question_mark));
                    errno = 0;
                    break;
                }

                // Print the frame information
                printf("%016lx\t%016lx\n",rbp, return_val);

                if (word == 1) {
                    //perror("ptrace");
                    //errno = 0;
                    // nothing more to read
                    break;
                }else if(word == -1){
                    perror("ptrace");
                    write(1, question_mark, strlen(question_mark));
                    errno = 0;
                    break;
                }
                
                // Move to the next stack frame
                rbp = word;
            }
            fflush(stdout);

            //unblock signal
            // Unblock all signals
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0) {
                perror("sigprocmask (unblocking)");
                write(1, question_mark, strlen(question_mark));
                continue;
            }

        }else{
            write(1, question_mark, strlen(question_mark));
        }

        // EOF / Quit
        /*if(exit_loop){
            break;
        }*/
    }

    //terminal = 1;
    // block sigchld signals
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
    }

    // closing statements from quiting
    for(int i=0; i<max_deet_processes; i++){
        if(deet_processes[i].state != PSTATE_DEAD && deet_processes[i].state != PSTATE_NONE){
            kill(deet_processes[i].process_id, SIGKILL);
            deet_processes[i].state = PSTATE_KILLED;
            //print_process(i);
            print_process_printf(i);
            
        }
    }
    
    // Wait for all child processes to exit (wait on no children return -1)
    // while (wait(NULL) > 0);
    wait_for_all_dead();

    // unblock all signals
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        write(STDOUT_FILENO, question_mark, strlen(question_mark));
    }

    exit(0);
}
