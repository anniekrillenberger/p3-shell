#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <termios.h>


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
} Job;

// pipeline of processes
typedef struct JobGroup {
    pid_t pgid;			// job group ID
    struct termios tmodes;	// saved terminal modes
} JobGroup;

// GLOBAL VARIABLES //

struct Job allJobs[256]; // array of jobs 
Job foregroundJob;
pid_t shellPGID;
struct termios shellTmodes;
int shellTerminal;
int isShellInteractive;


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
	
	// Put ourselves in our own process group 
        shellPGID = getpid();
      	if (setpgid (shellPGID, shellPGID) < 0) {
      	    char processGroupErr[256] = "Couldn't put the shell in its own process group\n";
      	    write(STDOUT_FILENO, processGroupErr, strlen(processGroupErr));
            exit(-1);
        }
        
        // Grab control of the terminal
      	tcsetpgrp (shellTerminal, shellPGID);

      	// Save default terminal attributes for shell
      	tcgetattr (shellTerminal, &shellTmodes);
      
    } // end of isShellInteractive
}


// code from GNU C Library
// TODO: add in piping
void launchJob(Job *job) {
    pid_t pid;
    
    if(isShellInteractive) {
        // put process into process group and give process group terminal if needed
        pid = getpid();
        if(pid == 0) {
            pgid = pid;
        }
        if(job -> isFG) {
            tcset(shellTerminal, pgid);
        }
        
        // Set the handling for job control signals back to the default
        signal (SIGINT,  SIG_DFL);
        signal (SIGQUIT, SIG_DFL);
        signal (SIGTSTP, SIG_DFL);
        signal (SIGTTIN, SIG_DFL);
        signal (SIGTTOU, SIG_DFL);
        signal (SIGCHLD, SIG_DFL);
    }
}


// implement commands specified by paths
int paths(char **args, int wshc) {
    char pathUsr[256] = "/usr/bin/";
    char path[256] = "/bin/";
    char absolutePath[256] = "";
    strcat(pathUsr, args[0]);
    strcat(path, args[0]);
    args[wshc] = NULL; // null terminate args
    if(access(pathUsr, X_OK) != 0) { // /usr/bin/command not executable
        if (access(path, X_OK) != 0) { // /bin/command not executable
            char notExecutable[256] = "Command is not executable\n";
            write(STDOUT_FILENO, notExecutable, strlen(notExecutable));
            exit(-1);
        } else { // reset path to /bin/command
            strcpy(absolutePath, path);
        }
    } else { // reset path to /usr/bin/command
        strcpy(absolutePath, pathUsr);
    }
    
    // fork exec !!! -- reference from discussion code
    pid_t pid = fork();
    if (pid == 0) { // child process !!
    
        launchJob();
    
        execvp(absolutePath, args);
        // if succeeds, should not reach here !!
        char execFailed[256] = "Exec failed\n";
        write(STDOUT_FILENO, execFailed, strlen(execFailed));
        exit(-1);
    } else { // parent process !!

        // add job to array - ID, isDone, isFG, programName, args, numArgs, & wasInitBG
        struct Job newJob;
	newJob.isDone = 0;
	newJob.wasInitBG = 0;
	newJob.numArgs = 0;
	newJob.programName = args[0];
	newJob.isFG = 1;
	foregroundJob = newJob;
	
    	for(int j = 0; j < 256; j++) { // find ID
            if(allJobs[j].isValid == 1) {
            	newJob.isValid = 0;
            	allJobs[j] = newJob;
                newJob.id = j + 1; // ID has to be positive
                break;
            }
	}
	
	int i = 1;
	while(args[i] != NULL) { // get wasInitBG, args, & numArgs
	
	    if(strcmp(args[i], "&") == 0) {
	         newJob.wasInitBG = 1;
	         break; // means we're at the end of args
	    }
	    
	    newJob.args[newJob.numArgs] = args[i];
	    newJob.numArgs++;    
	    i++;
	}
		
        int status;
        waitpid(pid, &status, 0); // wait for child process to finish
        
        return 1;
    }
}


/**
 * checks built in command bg
 *
 * returns 1 if command completed, 0 otherwise
 */
int bg() {
    return 0;
}


/**
 * checks built in command fg
 *
 * returns 1 if command completed, 0 otherwise
 */
int fg() {
    return 0;
}


/**
 * checks built in command jobs
 *
 * returns 1 if command completed, 0 otherwise
 */
int jobs(char **args, int wshc) {
    if (strncmp(args[0], "jobs", 4) == 0) { // check if user started with jobs
        if (wshc != 1) { // error! -- 1 arg is jobs
            char argError[256] = "the jobs command has no arguments\n";
            write(STDOUT_FILENO, argError, strlen(argError));
            exit(-1);
        }
        // TODO: account for piping when implemented
        // iterate through jobs & print out background jobs
        for(int i = 0; i < sizeof(allJobs) / sizeof(allJobs[0]); i++) {
            if(!(allJobs[i].isFG)) { // format: `<id>: <program name> <arg1> <arg2> … <argN> [&]`
                char formatted[256];
                sprintf(formatted, "%d: ", allJobs[i].id);
                strcat(formatted, allJobs[i].programName);
                write(STDOUT_FILENO, formatted, strlen(formatted));
                // print out all args
                for(int j = 0; j < 256; j++) { // TODO: better conditional :)
                    char arg[256] = " ";
                    strcat(arg, allJobs[i].args[j]);
                    write(STDOUT_FILENO, arg, strlen(arg));
                }
                if(allJobs[i].wasInitBG) { // if job was initiated as background
                    char initBG[256] = " &";
                    write(STDOUT_FILENO, initBG, strlen(initBG));
                }
                char *newLine = "\n";
                write(STDOUT_FILENO, newLine, strlen(newLine));
            } // else the job is in the foreground !
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
            exit(-1);
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
    if(fg()) return 1;

    // bg //
    if(bg()) return 1;

    return 0;
}


int main(int argc, char *argv[]) {
    char *userIn;
    int isBatch = 0;
    
    shellInit();
    
    // initialize jobs to be free
    for(int i = 0; i < 256; i++) allJobs[i].isValid = 1;

    // check if interactive mode or batch mode
    if (argc == 1) { // interactive mode
        // do nothing for now
    } else if (argc == 2) { // batch mode  -- format to call should be ./wsh scriptName
        // isBatch = 1;
        // open file n shit
    } else { // invalid input
        char invalidIn[256] = "Invalid input\n";
        write(STDOUT_FILENO, invalidIn, strlen(invalidIn));
        exit(-1);
    }

    while (1) { // repeatedly asks for input

        // print prompt if in interactive mode
        if (!isBatch) {
            char prompt[256] = "wsh> ";
            write(STDOUT_FILENO, prompt, strlen(prompt));
        }

        // get user input
        size_t len = 0;
        ssize_t getLine = getline(&userIn, &len, stdin);
        // null terminate to get rid of new line char
        userIn[getLine - 1] = '\0';

        // if user types ctrl-d, exit
        if (feof(stdin)) {
            exit(0);
        } else if (getLine == -1) { // error handling !!
            char lineReadError[256] = "Unable to read user input\n";
            write(STDOUT_FILENO, lineReadError, strlen(lineReadError));
            exit(-1);
        }

        // get number of arguments -- chatgpt for help with logic of string of strings
        char *args[256];
        char *seperate;
        int wshc = 0;
        while ((seperate = strsep(&userIn, " ")) != NULL) {
            if (strlen(seperate) > 0) {
                args[wshc] = strdup(seperate);
                wshc++;
            }
        }

        // built in commands: exit, cd, jobs, fg, & bg
        if (builtInCommands(args, wshc)) continue;

        // paths
        if(paths(args, wshc)) continue;
        
        char notHere[256] = "You should not be here!!";
        write(STDOUT_FILENO, notHere, strlen(notHere));

    } // end of while loop

    return 0;

} // end of main


