#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <termios.h>


// job code / logic from GNU C Library //

pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;

/* A process is a single process.  */
typedef struct process {
  struct process *next;       /* next process in pipeline */
  char **argv;                /* for exec */
  pid_t pid;                  /* process ID */
  char completed;             /* true if process has completed */
  char stopped;               /* true if process has stopped */
  int status;                 /* reported status value */
} process;

/* A job is a pipeline of processes.  */
typedef struct job {
    int isFG;               // boolean if job  foreground or background
    /**
  struct job *next;           // next active job
  char *command;              // command line, used for messages
  process *first_process;     // list of processes in this job
  pid_t pgid;                 // process group ID
  char notified;              // true if user told about stopped job
  struct termios tmodes;      // saved terminal modes
  int stdin, stdout, stderr;  // standard i/o channels*/
} job;

// array of jobs 
job *jobs[256];

// The active jobs are linked into a list. This is its head
job *first_job = NULL;

/* Find the active job with the indicated pgid.  */
job * find_job (pid_t pgid) {
  job *j;

  for (j = first_job; j; j = j->next)
    if (j->pgid == pgid)
      return j;
  return NULL;
}

/* Return true if all processes in the job have stopped or completed.  */
int job_is_stopped (job *j) {
  process *p;

  for (p = j->first_process; p; p = p->next)
    if (!p->completed && !p->stopped)
      return 0;
  return 1;
}

/* Return true if all processes in the job have completed.  */
int job_is_completed (job *j) {
  process *p;

  for (p = j->first_process; p; p = p->next)
    if (!p->completed)
      return 0;
  return 1;
}

/* Make sure the shell is running interactively as the foreground job
   before proceeding. */
void init_shell () {

  /* See if we are running interactively.  */
  shell_terminal = STDIN_FILENO;
  shell_is_interactive = isatty (shell_terminal);

  if (shell_is_interactive) {
      /* Loop until we are in the foreground.  */
      while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
        kill (- shell_pgid, SIGTTIN);

      /* Ignore interactive and job-control signals.  */
      signal (SIGINT, SIG_IGN);
      signal (SIGQUIT, SIG_IGN);
      signal (SIGTSTP, SIG_IGN);
      signal (SIGTTIN, SIG_IGN);
      signal (SIGTTOU, SIG_IGN);
      signal (SIGCHLD, SIG_IGN);

      /* Put ourselves in our own process group.  */
      shell_pgid = getpid ();
      if (setpgid (shell_pgid, shell_pgid) < 0) {
          perror ("Couldn't put the shell in its own process group");
          exit (-1);
        }

      /* Grab control of the terminal.  */
      tcsetpgrp (shell_terminal, shell_pgid);

      /* Save default terminal attributes for shell.  */
      tcgetattr (shell_terminal, &shell_tmodes);
    }
}

void launch_job (job *j, int foreground) {
  process *p;
  pid_t pid;
  int mypipe[2], infile, outfile;

  infile = j->stdin;
  for (p = j->first_process; p; p = p->next)
    {
      /* Set up pipes, if necessary.  */
      if (p->next)
        {
          if (pipe (mypipe) < 0)
            {
              perror ("pipe");
              exit (1);
            }
          outfile = mypipe[1];
        }
      else
        outfile = j->stdout;

      /* Fork the child processes.  */
      pid = fork ();
      if (pid == 0)
        /* This is the child process.  */
        launch_process (p, j->pgid, infile,
                        outfile, j->stderr, foreground);
      else if (pid < 0)
        {
          /* The fork failed.  */
          perror ("fork");
          exit (1);
        }
      else
        {
          /* This is the parent process.  */
          p->pid = pid;
          if (shell_is_interactive)
            {
              if (!j->pgid)
                j->pgid = pid;
              setpgid (pid, j->pgid);
            }
        }

      /* Clean up after pipes.  */
      if (infile != j->stdin)
        close (infile);
      if (outfile != j->stdout)
        close (outfile);
      infile = mypipe[0];
    }

  format_job_info (j, "launched");

  if (!shell_is_interactive)
    wait_for_job (j);
  else if (foreground)
    put_job_in_foreground (j, 0);
  else
    put_job_in_background (j, 0);
}

/* Put job j in the foreground.  If cont is nonzero,
   restore the saved terminal modes and send the process group a
   SIGCONT signal to wake it up before we block.  */
void put_job_in_foreground (job *j, int cont) {
  /* Put the job into the foreground.  */
  tcsetpgrp (shell_terminal, j->pgid);

  /* Send the job a continue signal, if necessary.  */
  if (cont) {
      tcsetattr (shell_terminal, TCSADRAIN, &j->tmodes);
      if (kill (- j->pgid, SIGCONT) < 0)
        perror ("kill (SIGCONT)");
    }

  /* Wait for it to report.  */
  wait_for_job (j);

  /* Put the shell back in the foreground.  */
  tcsetpgrp (shell_terminal, shell_pgid);

  /* Restore the shell’s terminal modes.  */
  tcgetattr (shell_terminal, &j->tmodes);
  tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);
}

/* Put a job in the background.  If the cont argument is true, send
   the process group a SIGCONT signal to wake it up.  */
void put_job_in_background (job *j, int cont) {
  /* Send the job a continue signal, if necessary.  */
  if (cont)
    if (kill (-j->pgid, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
}

/* Store the status of the process pid that was returned by waitpid.
   Return 0 if all went well, nonzero otherwise.  */
int mark_process_status (pid_t pid, int status) {
  job *j;
  process *p;

  if (pid > 0) {
      /* Update the record for the process.  */
      for (j = first_job; j; j = j->next)
        for (p = j->first_process; p; p = p->next)
          if (p->pid == pid)
            {
              p->status = status;
              if (WIFSTOPPED (status))
                p->stopped = 1;
              else
                {
                  p->completed = 1;
                  if (WIFSIGNALED (status))
                    fprintf (stderr, "%d: Terminated by signal %d.\n",
                             (int) pid, WTERMSIG (p->status));
                }
              return 0;
             }
      fprintf (stderr, "No child process %d.\n", pid);
      return -1;
    } else if (pid == 0 || errno == ECHILD) {
        /* No processes ready to report.  */
        return -1;
    } else { /* Other weird errors.  */
        perror ("waitpid");
        return -1;
    }
}

/* Check for processes that have status information available,
   without blocking.  */
void update_status (void) {
  int status;
  pid_t pid;

  do
    pid = waitpid (WAIT_ANY, &status, WUNTRACED|WNOHANG);
  while (!mark_process_status (pid, status));
}

/* Check for processes that have status information available,
   blocking until all processes in the given job have reported.  */
void wait_for_job (job *j) {
  int status;
  pid_t pid;

  do
    pid = waitpid (WAIT_ANY, &status, WUNTRACED);
  while (!mark_process_status (pid, status)
         && !job_is_stopped (j)
         && !job_is_completed (j));
}

/* Format information about job status for the user to look at.  */
void format_job_info (job *j, const char *status) {
  fprintf (stderr, "%ld (%s): %s\n", (long)j->pgid, status, j->command);
}

/* Notify the user about stopped or terminated jobs.
   Delete terminated jobs from the active job list.  */
void do_job_notification (void) {
  job *j, *jlast, *jnext;

  /* Update status information for child processes.  */
  update_status ();

  jlast = NULL;
  for (j = first_job; j; j = jnext) {
      jnext = j->next;

      /* If all processes have completed, tell the user the job has
         completed and delete it from the list of active jobs.  */
      if (job_is_completed (j)) {
        format_job_info (j, "completed");
        if (jlast)
          jlast->next = jnext;
        else
          first_job = jnext;
        free_job (j);
      }

      /* Notify the user about stopped jobs,
         marking them so that we won’t do this more than once.  */
      else if (job_is_stopped (j) && !j->notified) {
        format_job_info (j, "stopped");
        j->notified = 1;
        jlast = j;
      }

      /* Don’t say anything about jobs that are still running.  */
      else
        jlast = j;
    }
}

/* Mark a stopped job J as being running again.  */
void mark_job_as_running (job *j) {
  Process *p;

  for (p = j->first_process; p; p = p->next)
    p->stopped = 0;
  j->notified = 0;
}

/* Continue the job J.  */
void continue_job (job *j, int foreground) {
  mark_job_as_running (j);
  if (foreground)
    put_job_in_foreground (j, 1);
  else
    put_job_in_background (j, 1);
}


/**
 * checks built in commands: exit, cd, jobs, fg, & bg
 *
 * returns 1 if command completed, 0 otherwise
 */
int builtInCommands(char **args, int wshc) {

    // if user types exit, exit
    if (strcmp(args[0], "exit") == 0) {
        exit(0);
    }

    // cd
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
    // TODO: jobs, fg, & bg :/
}

// implement commands specified by paths
void paths(char **args, int wshc) {
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
        execvp(absolutePath, args);
        // if succeeds, should not reach here !!
        char execFailed[256] = "Exec failed\n";
        write(STDOUT_FILENO, execFailed, strlen(execFailed));
        exit(-1);
    } else { // parent process !!

        // iterate through job list to find smallest positive integer not in use
        for(int i = 1; i < sizeof(jobs) / sizeof(jobs[1]); i++) {
            if(jobs[i] == NULL) { // empty space
                jobs[i] = malloc(sizeof(job));
            }
        }

        int status;
        waitpid(pid, &status, 0); // wait for child process to finish
    }
}

int main(int argc, char *argv[]) {
    char *userIn;
    int isBatch = 0;

    // check if interactive mode or batch mode
    if (argc == 1) { // interactive mode
        // do nothing for now
    } else if (argc == 2) { // batch mode  -- format should be ./wsh scriptName
        // isBatch = 1;
        // open file n shit
    } else { // invalid input
        char invalidIn[256] = "Invalid input\n";
        write(STDOUT_FILENO, invalidIn, strlen(invalidIn));
        exit(-1);
    }

    // repeatedly asks for input
    while (1) {

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

        // exit, cd, jobs, fg, & bg
        if (builtInCommands(args, wshc))
            continue;

        // paths
        paths(args, wshc);

    } // end of while loop

    return 0;

} // end of main
