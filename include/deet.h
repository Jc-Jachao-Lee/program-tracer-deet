/*
 * DO NOT MODIFY THE CONTENTS OF THIS FILE.
 * IT WILL BE REPLACED DURING GRADING
 */
#ifndef DEET_H
#define DEET_H

#include <sys/types.h>

/*
 * Process states.
 */
typedef enum pstate {
    PSTATE_NONE,	// State of a nonexistent process.
    PSTATE_RUNNING,	// State of a process immediately following successful fork().
    PSTATE_STOPPING,	// State of the process while attempting to stop it,
                        // but before receiving the subsequent notification via SIGCHLD.
    PSTATE_STOPPED,	// State of the process once it is known to have stopped.
    PSTATE_CONTINUING,	// State of the process after having continued it,
                        // but before receiving the subsequent notification via SIGCHLD
                        // that the process has continued.
    PSTATE_KILLED,	// State of the process after sending SIGKILL but before
                        // receiving the subsequent notification via SIGCHLD
                        // that the process has terminated.
    PSTATE_DEAD		// State of the process once it is known to be terminated.
} PSTATE;

#endif
