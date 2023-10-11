#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <fcntl.h>

// job code / logic from GNU C Library //

typedef struct Job {
    int id;             // key
    int isDone;		// keeps track if job is done 
    int isFG;           // boolean if job is foreground or background
    char *programName;	// name of program
    char *args[256];	// string array of args
    int numArgs;	// length of args
    int wasInitBG;	// if the job was initiated as bg (with an &)
    int isValid;	// 0 if space in use, 1 otherwise
    pid_t pid;		// 
    pid_t pgid;		// 
} Job;

// GLOBAL VARIABLES //
struct Job allJobs[256]; // array of background & stopped jobs 
Job foregroundJob;
pid_t shellPGID;
struct termios shellTmodes;
int shellTerminal;
int isShellInteractive;
volatile sig_atomic_t sigChildFlag = 0;
volatile sig_atomic_t sigStopFlag = 0;
int pipeFile;
int isPipe;


// Handle SIGCHLD (child process dies)
void sigchild_handler(int sigchild) {
    sigChildFlag = 1;
    
    int status;
    // block until background process is done
    waitpid(WAIT_ANY, &status, WUNTRACED|WNOHANG); // PUT IN WHILE LOOP !!

} // gets called for any changed -- use status to find out!!


// add job to array - ID, isDone, isFG, programName, args, numArgs, & wasInitB
Job addJob(char **args, int wshc, pid_t pid) {
    struct Job newJob;
    newJob.isDone = 0;
    newJob.wasInitBG = 0;
    newJob.numArgs = 0;
    newJob.programName = args[0];
    newJob.pid = pid;

    int i = 1;
    while(args[i] != NULL) { // get args & numArgs
 	newJob.args[newJob.numArgs] = args[i];
        newJob.numArgs++;    
        i++;
     }
     
     if(strcmp(args[wshc - 1], "&") == 0) { // wasInitGB & isFG
        newJob.wasInitBG = 1;
	    newJob.isFG = 0;
     }
     
     if(!newJob.wasInitBG) { // forground process
         newJob.isFG = 1;
    	 foregroundJob = newJob;
     } else {
         for(int j = 0; j < 256; j++) { // find ID
            if(allJobs[j].isValid == 1) {
                newJob.isValid = 0;
                newJob.id = 1 + j; // ID has to be positive
                allJobs[j] = newJob;
                break;
            }
        }
     }
     
     return newJob;
}


// code from GNU C Library
void launchJob(Job job, pid_t pgid, char **args, int fg, int wshc, char path[]) {
    pid_t pid;
    
    if(isShellInteractive) {
        // put process into process group and give process group terminal if needed
        pid = getpid();
        if(pgid == 0) {
            pgid = pid;
            job.pgid = pgid;
        }
        
        setpgid(pid, pgid);
        
        // if job is foreground
        if(fg) { // sending currjob to current terminal if in fg
            job.isFG = 1;
            tcsetpgrp(shellTerminal, pgid);
        }
        
        // Set the handling for job control signals back to the default
        signal (SIGINT,  SIG_DFL);
        signal (SIGQUIT, SIG_DFL);
        signal (SIGTSTP, SIG_DFL);
        signal (SIGTTIN, SIG_DFL);
        signal (SIGCHLD, SIG_DFL);
        signal (SIGCONT, SIG_DFL);
    }
        
    execvp(path, args);
        
    // if succeeds, should not reach here !!
    char execFailed[256] = "Exec failed\n";
    write(STDOUT_FILENO, execFailed, strlen(execFailed));
    exit(-1);
}


/* Put a job in the background.  If the cont argument is true, send
 * the process group a SIGCONT signal to wake it up.  
 */
void putInBG(Job job, int cont) {

    // send the job a continue signal if necessary
    if(cont) {
        if(kill (-job.pgid, SIGCONT) < 0) {
            char killFailed[256] = "kill SIGCONT error (BG)\n";
    	    write(STDOUT_FILENO, killFailed, strlen(killFailed));
        }
    } else {
        // Set the process group to be in the background
        if (tcsetpgrp(shellTerminal, shellPGID) < 0) {
            char tcsetpgrp[256] = "tcsetpgrp (putInBG)\n";
    	    write(STDOUT_FILENO, tcsetpgrp, strlen(tcsetpgrp));
        }
    }

    // Ignore SIGINT and SIGTSTP
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
}


/** Put job in the foreground.  If cont is nonzero,
  * restore the saved terminal modes and send the process group a
  * SIGCONT signal to wake it up before we block.  
*/
void putInFG(Job job, int cont) {
    foregroundJob = job;

    // Put the job into the foreground
    tcsetpgrp (shellTerminal, job.pgid);

    // Send the job a continue signal, if necessary
    if (cont) {
      if (kill (- job.pgid, SIGCONT) < 0) {
          char killFailed[256] = "kill SIGCONT error(FG)\n";
    	  write(STDOUT_FILENO, killFailed, strlen(killFailed));
      }
    }

    int status;
    waitpid(foregroundJob.pid, &status, WUNTRACED);

    // Check if the job was stopped by SIGTSTP
    if (WIFSTOPPED(status) && job.isFG) {
        // printf("Job [%d] stopped by signal SIGTSTP\n", job.id);
        // ADD JOB TO JOB LIST -- help from Omid
        foregroundJob.isValid = 1;
        job.isFG = 0;
        for(int j = 0; j < 256; j++) { // find ID
            if(allJobs[j].isValid == 1) {
                job.isValid = 0;
                job.id = 1 + j; // ID has to be positive
                allJobs[j] = job;
                break;
            }
        }
    }

    // Clear the foreground job
    foregroundJob.isValid = 1;
    
    // return control to the shell
    tcsetpgrp(shellTerminal, shellPGID); // CORRECT -- confirmed by Omid
}


// code from GNU C Library
void shellInit() {
    // get process group ID of current terminal 
    pid_t initpgid = tcgetpgrp(STDIN_FILENO);
    if(initpgid == -1) {
        char pgidErr[256] = "Error getting current terminal process ID\n";
        write(STDOUT_FILENO, pgidErr, strlen(pgidErr));
        exit(-1);
    }
    
    // TODO: check if running interactively when implementing batch mode
    // Make sure the shell is running interactively as the foreground job
    shellTerminal = STDIN_FILENO;
    isShellInteractive = isatty(shellTerminal);
    if(isShellInteractive) {
        // loop until in foreground
        while(tcgetpgrp(shellTerminal) != (shellPGID = getpgrp()))
            kill (- shellPGID, SIGTTIN);
            
        // Ignore interactive and job-control signals
    	signal (SIGINT,  SIG_IGN);
        signal (SIGQUIT, SIG_IGN);
        signal (SIGTSTP, SIG_IGN);
        signal (SIGTTIN, SIG_IGN);
        signal (SIGTTOU, SIG_IGN);
        signal (SIGCHLD, SIG_IGN);
        
        // Put ourselves in our own process group -- from piazza
        shellPGID = getpid();
        if(getpid() != getsid(0)) {
            // the shell is not the session leader :)
            if (setpgid (shellPGID, shellPGID) < 0) {
                char processGroupErr[256] = "Couldn't put the shell in its own process group\n";
                write(STDOUT_FILENO, processGroupErr, strlen(processGroupErr));
                exit(-1);
            }
        }
        
        // Grab control of the terminal
      	tcsetpgrp (shellTerminal, shellPGID);

      	// Save default terminal attributes for shell
      	tcgetattr (shellTerminal, &shellTmodes);
      
    } // end of isShellInteractive
}


// implement commands specified by paths
int paths(char **args, int wshc) {

    char pathUsr[256] = "/usr/bin/";
    char path[256] = "/bin/";
    char absolutePath[256] = "";
    args[wshc] = NULL; // null terminate args

    if(isPipe) { // piping -- deal w file
        // If it's part of a pipe and output redirection is needed, open the output file
        pipeFile = open("pipe1.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

        if (pipeFile == -1) {
            char failedToOpen[256] = "Failed to open output file\n";
            write(STDOUT_FILENO, failedToOpen, strlen(failedToOpen));
            return -1;
        }
    } 

    strcat(pathUsr, args[0]);
    strcat(path, args[0]);
    
    if(access(pathUsr, X_OK) != 0) { // /usr/bin/command not executable
        if (access(path, X_OK) != 0) { // /bin/command not executable
            char notExecutable[256] = "Command is not executable\n";
            write(STDOUT_FILENO, notExecutable, strlen(notExecutable));
            return -1;
        } else // reset path to /bin/command
            strcpy(absolutePath, path);
    } else // reset path to /usr/bin/command
        strcpy(absolutePath, pathUsr);
    
    pid_t pid = fork();
    struct Job job; 
    job = addJob(args, wshc, pid); // ONLY PARENTS SHOULD HAVE ACCESS TO THIS
    
    int fg = 1; // default to foreground
    // check if initialized to background
    if(wshc > 0 && job.wasInitBG) {
        fg = 0;
        args[wshc - 1] = NULL;
    }
    
    // fork exec !!! -- reference from discussion code
    if (pid == 0) { // child process !! 
       	job.pid = getpid();
 	    pid_t pgid = 0;

        if(isPipe) {
            // Redirect stdout to the file
            dup2(pipeFile, STDOUT_FILENO);

        }
 	
        launchJob(job, pgid, args, fg, wshc, absolutePath);
        
    } else if (pid > 0) { // parent process !!
        if(isShellInteractive) {
            if(!job.pgid) {
                job.pgid = pid;
            }
            setpgid(pid, job.pgid);
            
            if(fg) { // job in foreground
                putInFG(job, 0);
                foregroundJob = job;
            } else {
                // wait pid for sigchild -- different waitpid
        	    signal(SIGCHLD, sigchild_handler);
                putInBG(job, 0);
            }
        } // end of if(isShellInteractive) 
        
    } else { // fork failed
        char forkFail[256] = "Fork Failed\n";
        write(STDOUT_FILENO, forkFail, strlen(forkFail));
        return -1;
    }

    // remove job from job list & autoremoves from ps
    if(sigChildFlag) { // get rid of inactive background job
        for (int j = 0; j < 256; j++) {
            if (!allJobs[j].isFG && !allJobs[j].isValid) {
                // Send SIGCONT to the process group associated with the job
                if (kill(-allJobs[j].pgid, SIGCONT) < 0) {
                    char kill[256] = "kill (removing job)\n";
                    write(STDOUT_FILENO, kill, strlen(kill));
                    return -1;
                }

                // Clear the job entry
                allJobs[j].isValid = 1;
                allJobs[j].id = 0;
            }
        }
    }
    return 0;
}


// ** BUILT IN COMMANDS ** //


/**
 * resume a stopped background job
 * release SINGCONT and update job list
 *
 * returns 1 if command completed, 0 otherwise
 */
int bg(char **args, int wshc) {
    // check if user started with bg
    if (strncmp(args[0], "bg", 2) != 0) return 0;

    if (wshc != 1 && wshc != 2) { // bg has no or one arg
        char argError[256] = "the bg command has no or 1 argument(s)\n";
        write(STDOUT_FILENO, argError, strlen(argError));
        return -1;
    } 

    int jobID;
    if(wshc == 2) {
        jobID = atoi(args[1]);
        if(!jobID) {
            char numError[256] = "Argument for bg must be a number\n";
            write(STDOUT_FILENO, numError, strlen(numError));
            return -1;
        }
    } else { // Determine the largest job ID
        jobID = -1;
        for(int i=0; i<256; i++) {
        	if(!allJobs[i].isValid && allJobs[i].id > jobID && !allJobs[i].isFG) {
                jobID = allJobs[i].id;
            }
        }
        if (jobID == -1) {
            char noJobsError[256] = "No jobs available in bg\n";
            write(STDOUT_FILENO, noJobsError, strlen(noJobsError));
            return -1;
        }
    }

    // Find the job with the specified ID
    int found = 0;
    for (int i = 0; i < 256; i++) {
        if (!allJobs[i].isValid && !allJobs[i].isFG && allJobs[i].id == jobID) {
            // Send SIGCONT to the process group associated with the job
            if (kill(-allJobs[i].pgid, SIGCONT) < 0) {
                char kill[256] = "kill\n";
                write(STDOUT_FILENO, kill, strlen(kill));
                return -1;
            }

            // Update job status
            allJobs[i].isDone = 0;

            found = 1;
            break;
        }
    }

    if (!found) {
        char notFoundMsg[256];
        sprintf(notFoundMsg, "No background job found with ID %d\n", jobID);
        write(STDOUT_FILENO, notFoundMsg, strlen(notFoundMsg));
        return -1;
    }

    return 1;
}


/**
 * moves a job that is stopped or running in bg to the fg
 *
 * returns 1 if command completed, 0 otherwise
 */
int fg(char **args, int wshc) {
    // check if user started with bg
    if (strncmp(args[0], "fg", 2) != 0) return 0;

    if (wshc != 1 && wshc != 2) { // fg has no or one arg
        char argError[256] = "the fg command has no or 1 argument(s)\n";
        write(STDOUT_FILENO, argError, strlen(argError));
        return -1;
    }

    int jobID;
    if(wshc == 2) {
        jobID = atoi(args[1]);
        if(!jobID) {
            char numError[256] = "Argument for fg must be a number\n";
            write(STDOUT_FILENO, numError, strlen(numError));
            return -1;
        }
    } else { // Determine the largest job ID
        jobID = -1;
        for(int i=0; i<256; i++) {
        	if(!allJobs[i].isValid && allJobs[i].id > jobID && !allJobs[i].isFG) {
                jobID = allJobs[i].id;
            }
        }
        if (jobID == -1) {
            char noJobsError[256] = "No jobs available in bg\n";
            write(STDOUT_FILENO, noJobsError, strlen(noJobsError));
            return -1;
        }
    }

    // find job associated with id
    for(int i = 0; i < 256; i++) {
        if (!allJobs[i].isValid && allJobs[i].id == jobID) {
            allJobs[i].isFG = 1;
            foregroundJob = allJobs[i];
            allJobs[i].isValid = 1;
            putInFG(allJobs[i], 1);
        }
    }
     if (jobID == -1) {
        char invalidID[256] = "Invalid ID\n";
        write(STDOUT_FILENO, invalidID, strlen(invalidID));
        return -1;
    }
    return 1;
}


/**
 * checks built in command jobs
 * format: `<id>: <program name> <arg1> <arg2> … <argN> [&]`
 *
 * returns 1 if command completed, 0 otherwise
 */
int jobs(char **args, int wshc) {
    if (strncmp(args[0], "jobs", 4) == 0) { // check if user started with jobs
        if (wshc != 1) { // error! -- 1 arg is jobs
            char argError[256] = "the jobs command has no arguments\n";
            write(STDOUT_FILENO, argError, strlen(argError));
            return -1;
        }
        // TODO: account for piping when implemented
        // iterate through jobs & print out background jobs
        for(int i = 0; i < 256; i++) {
            if(!allJobs[i].isValid && !allJobs[i].isFG) {
            
                char formatted[256];
                sprintf(formatted, "%d: %s", allJobs[i].id, allJobs[i].programName);
                // print out all args
                for(int j = 0; j < allJobs[i].numArgs; j++) {
                    strcat(formatted, " ");
                    strcat(formatted, allJobs[i].args[j]);
                } 
            
                strcat(formatted, "\n");
                write(STDOUT_FILENO, formatted, strlen(formatted));
            }
        }
        return 1;
    }
    return 0;
}


/**
 * checks built in commands: exit & cd
 *
 * returns 1 if command completed, 0 otherwise
 */
int exitAndCD(char **args, int wshc) {

    // if user types exit, exit
    if (strcmp(args[0], "exit") == 0) exit(0);

    // cd //
    if (strncmp(args[0], "cd", 2) == 0) { // check if user started with cd
        if (wshc != 2) { // error! -- account for cd arg
            char argError[256] = "Incorrect number of arguments for cd\n";
            write(STDOUT_FILENO, argError, strlen(argError));
            return -1;
        }
        // system call with file path given by user
        chdir(args[1]);
        return 1;
    }
    return 0;
}


/**
 * checks built in commands: exit, cd, jobs, fg, & bg
 *
 * returns 1 if command completed, 0 otherwise
 */
int builtInCommands(char **args, int wshc) {

    // exit & cd commands //
    if(exitAndCD(args, wshc)) return 1;

    // jobs //
    if(jobs(args, wshc)) return 1;

    // fg //
    if(fg(args, wshc)) return 1;

    // bg //
    if(bg(args, wshc)) return 1;

    return 0;
}


int main(int argc, char *argv[]) {
    char *userIn;
    FILE *fileIn;
    int isInteractive = 0;
    
    shellInit();
    
    // initialize jobs to be free
    for(int i = 0; i < 256; i++) allJobs[i].isValid = 1;

    // check if interactive mode or batch mode
    if (argc == 1) { // interactive mode
        isInteractive = 1;
    } else if (argc == 2) { // batch mode  -- format to call should be ./wsh scriptName
        fileIn = fopen(argv[1], "r");
        if(fileIn == NULL) {
            char fnf[256] = "File not found\n";
            write(STDOUT_FILENO, fnf, strlen(fnf));
            exit(-1);
        }
    } else { // invalid input
        char invalidIn[256] = "Invalid input\n";
        write(STDOUT_FILENO, invalidIn, strlen(invalidIn));
        return -1;
    }

    while (1) { // repeatedly asks for input

        // print prompt if in interactive mode
        if (isInteractive) {
            char prompt[256] = "wsh> ";
            write(STDOUT_FILENO, prompt, strlen(prompt));
        } 

        // get user input
        size_t len = 0;
        ssize_t getLine;
        if(isInteractive) { // interactive mode
            getLine = getline(&userIn, &len, stdin);
            // null terminate to get rid of new line char
            userIn[getLine - 1] = '\0';
        } else { // batch mode
            getLine = getline(&userIn, &len, fileIn);
            userIn[getLine - 1] = '\0';
        }

        // if user types ctrl-d, exit
        if (feof(stdin)) {
            exit(0);
        } else if (getLine == -1) { // error handling !!
            char lineReadError[256] = "Unable to read user input\n";
            write(STDOUT_FILENO, lineReadError, strlen(lineReadError));
            exit(-1);
        } else if (userIn[0] == '\0') {
            continue;
        }

        // piping
        isPipe = 0;
        int pipeIndex = -1;

        // get number of arguments -- chatgpt for help with logic of string of strings
        char *args[256];
        char *seperate;
        int wshc = 0;
        while ((seperate = strsep(&userIn, " ")) != NULL) {
            if (strlen(seperate) > 0) {
                if(strcmp(seperate, "|") == 0) { // it is a pipe
                    isPipe = 1;
                    pipeIndex = wshc;
                }
                args[wshc] = strdup(seperate);
                wshc++;
            }
        }

        if(!isPipe) { // (assumming there is only |)

            // built in commands: exit, cd, jobs, fg, & bg
            if (builtInCommands(args, wshc)) continue;

            // paths
            paths(args, wshc);

        } else { // piping
            char *pipe1[256];
            char *pipe2[256];
            int wshcPipe1 = 0;
            int wshcPipe2 = 0;

            for (int i = 0; i < pipeIndex; i++) {
                if (args[i] != NULL) {
                    pipe1[i] = strdup(args[i]);  // Copy each element
                    wshcPipe1++;
                } else {
                    pipe1[i] = NULL;  // Set the corresponding element in pipe1 to NULL
                }
            }

            paths(pipe1, wshcPipe1);
            close(pipeFile);

            // get second command
            for(int j = pipeIndex + 1; j < wshc; j++) {
                if (args[j] != NULL) {
                    pipe2[wshcPipe2] = strdup(args[j]);  // Copy each element
                    wshcPipe2++;
                } else {
                    pipe2[j] = NULL;  // Set the corresponding element in pipe1 to NULL
                }
            }

            // add output from command one to command 2
            FILE *file;
            isPipe = 0;
            // char pipeArgs[256];
            // int pipeArgCnt = 0;

            file = fopen("pipe1.txt", "r");
            if (file == NULL) {
                char failedToOpen[256] = "Failed to open input file\n";
                write(STDOUT_FILENO, failedToOpen, strlen(failedToOpen));
                return -1;
            }

            pipe2[wshcPipe2] = strdup("pipe1.txt");

            paths(pipe2, wshc - pipeIndex);
            fclose(file);
        }

    } // end of while loop

    return 0;

} // end of main


