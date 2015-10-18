#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/wait.h>
#include <unistd.h>
#include <termios.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <glob.h>

#define SIGDEFAULT 1
#define SIGIGNORE 0
#define CMDSIZE 256
#define MAXPIPE 64

#define PIPE 1
#define BUILTIN 2
#define NONBUILTIN 3

typedef struct process {
  int type;
  int arg_count;
  pid_t pid;
  char cmd[CMDSIZE];
  char *arguments[CMDSIZE];
  int status;
  int completed;
  int suspended;
  struct process* next;
} Process;

typedef struct job {
  Process *process_list;
  int job_num;
  pid_t pgid;
  char cmd[CMDSIZE];
  struct job *next;
} Job;

// keep track of the shell
pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;

// process and job control
char current_dir[PATH_MAX+1];
char *line;
size_t lineSize;
size_t numtokens;
int numpipes;
int state = 1;
Process *process_list = NULL;
int pipes[MAXPIPE][2];
Job *job_list = NULL;

int builtInCommands();
void cleanup();
void delete_job(int job_num);
void wildcard_expand(char *token, char *buf);
Job* find_job(int job_num);
void launch_process(Process *p, int in, int out, pid_t pgid);
void handler();
void init_shell();
void init_job(Job *new_job);
void init_process(Process *command);
int job_is_completed(Job *job);
int job_is_suspended(Job *job);
void my_bg();
void my_cd();
void my_exit();
void my_fg();
void my_jobs();
void printarg();
void prompt();
void split_line();
void put_job_in_fg(Job *job, int signal);
int update_process(Job *job, pid_t pid, int status);
void wait_for_job(Job *job);

void init_job(Job *new_job) {
  Job* j = job_list;
  new_job->pgid = 0;
  new_job->process_list = process_list;
  strcpy(new_job->cmd, line);
  new_job->next = NULL;
  // if it's the first job
  if (!j) {
    job_list = new_job;
    new_job->job_num = 1;
  } else {
    while (j->next)
      j = j->next;
    new_job->job_num = j->job_num + 1;
    j->next = new_job;
  }

  printf("[%d]: %s\n", new_job->job_num, new_job->cmd);
  printf("Init joblist successfully.\n");
}

Job *find_job (int job_num) {
  Job *j;
  for (j = job_list; j; j = j->next)
    if (j->job_num == job_num)
      return j;
  return NULL;
}

void delete_job(int job_num) {
  Job *jptr, *del = NULL, *pre = NULL;
  jptr = job_list;
  while (jptr != NULL) {
    if (jptr->job_num == job_num) {
      del = jptr;
      jptr = jptr->next;
      break;
    } else {
      pre = jptr;
      jptr = jptr->next;
    }
  }
  printf("\t\t\tdel: %s\n", del->cmd);
  if (del == NULL){
    printf("Nothing to delete");
  } else {
    if (!pre && !jptr) { // only one jog
      job_list = NULL;
    } else if (pre && jptr) { // in the middle
      pre->next = jptr;
      while (jptr != NULL) {
        (jptr->job_num)--;
        jptr = jptr->next;
      }
    } else if (!pre && jptr) { // delete the first job
      job_list = jptr;
      while (jptr != NULL) {
        (jptr->job_num)--;
        jptr = jptr->next;
      }
    } else if (pre && !jptr) { // delete the last job
        pre->next = NULL;
    } 
  }
  printf("Delete Job %d\n", del->job_num);
  Process *p = del->process_list, *tmp = NULL;
  while (p) {
    tmp = p;
    cleanup(p);
    p = tmp->next;
  }
  free(del);
  my_jobs();
}

void split_line() {
  char *token;
  Process *p = NULL, *tmp;
  int commandflag = 1;
  numtokens = 0;
  numpipes = 0;
  char copy[CMDSIZE];
  strcpy(copy, line);
  token = strtok(copy, " ");
  
  while(token != NULL) {
    if (strlen(token) == 1 && token[0] == '|') {
      commandflag = 1;
      numpipes++;
    }

    // if it's a command
    if (commandflag) {
      printf("cmd: %s\n", token);
      tmp = p;
      p = (Process *)malloc(sizeof(Process));
      if (tmp)
        tmp->next = p;
      init_process(p);
      strcpy(p->cmd, token);
      p->arguments[p->arg_count] = (char*)malloc(sizeof(char) * (strlen(token) + 1));
      strcpy(p->arguments[p->arg_count++], token);

      if ( !strcmp(p->cmd, "cd") || !strcmp(p->cmd, "exit") \
        || !strcmp(p->cmd, "jobs") || !strcmp(p->cmd, "fg"))
        p->type = BUILTIN;
      else if (!strcmp(p->cmd, "|"))
        p->type = PIPE;
      else
        p->type = NONBUILTIN; 

      if (p->type != PIPE)
        commandflag = 0;  
      } else {
      // it's argument
        printf("\targs: %s\n", token);
        if ((strchr(token, '*')) != NULL) {
          char buf[CMDSIZE] = "\0";
          wildcard_expand(token, buf);
          char *tok;
          tok = strtok(buf, " ");
          while (tok) {
            p->arguments[p->arg_count] = (char*)malloc(sizeof(char) * (strlen(tok) + 1));
            strcpy(p->arguments[p->arg_count++], tok);
            tok = strtok(NULL, " ");
          }
        } else {
          p->arguments[p->arg_count] = (char*)malloc(sizeof(char) * (strlen(token) + 1));
          strcpy(p->arguments[p->arg_count++], token);
        }
      }
    numtokens++;

    // set the first process as process head
    if (numtokens == 1)
      process_list = p;

    token = strtok(NULL, " ");
  }

  // get rid of the 'pipe' process
  for (p = process_list; p && p->next; p = p->next) {
    if (p->next->type == PIPE) {
      tmp = p->next;
      p->next = tmp->next;
      cleanup(tmp);
    }
  }

  printf("Number of pipes: %d\n\n", numpipes);
}


void wildcard_expand(char *token, char *buf) {
  glob_t results;
  int i;
  //char buf[CMDSIZE] = "\0";

  glob(token, GLOB_NOCHECK, NULL, &results);
  for (i = 0; i < results.gl_pathc; i++) {
    strcat(buf, results.gl_pathv[i]);
    strcat(buf, " ");
  }
  globfree(&results);
    
}

void init_process(Process *p){
  p->type = 0;
  p->arg_count = 0;
  p->next = NULL;
  p->pid = 0;
  p->status = 0;
  p->completed = 0;
  p->suspended = 0;
}

void launch_job(Job* job) {
  pid_t pid;
  Process *p = job->process_list;
  int mypipe[2], in = STDIN_FILENO, out;

  while (p) {
    if (p->next) {
      if (pipe(mypipe) < 0) {
        printf("Cannot create pipe\n");
        exit(-1);
      } else {
        out = mypipe[STDOUT_FILENO];
      }
    } else {
      out = STDOUT_FILENO;
    }

    pid = fork();
    if (pid == 0) {
      pid = getpid();
      if (!job->pgid) {
        job->pgid = pid;
        printf("head process, set gpid %d\n", job->pgid);
      }
      p->pid = pid;
      launch_process(p, in, out, job->pgid);
    } else if (pid < 0) {
      printf("Cannot create child process.\n");
    } else {
      if (!job->pgid)
        job->pgid = pid;
      p->pid = pid;
      if (setpgid(pid, job->pgid) < 0) {
        printf("Cannot put child intuo proc group: %d\n", job->pgid);
        exit(1);
      }
    }
    if (in != STDIN_FILENO)
      close(in);
    if (out != STDOUT_FILENO)
      close(out);
    in = mypipe[STDIN_FILENO];
    p = p->next;
  }

  put_job_in_fg(job, 0);

}

void put_job_in_fg(Job *job, int signal) {
  Process *p = NULL;
  int is_suspended = 0;
  tcsetpgrp(STDIN_FILENO, job->pgid);
  printf("Now Input is %d\n",tcgetpgrp(STDIN_FILENO));
  // tcsetpgrp(STDOUT_FILENO, job->pgid);
  // if resume
  if (signal)
    kill(- job->pgid, SIGCONT);
  
  // perror("Error:");
  printf("\tjob id: %d\n", job->pgid);
  printf("\tforeground job id: %d\n", tcgetpgrp(STDIN_FILENO));


  wait_for_job(job);

  printf("Shell pgid: %d\n",shell_pgid);
  tcsetpgrp(STDIN_FILENO, shell_pgid);
  printf("Shell is now: [%d]\n",tcgetpgrp(STDIN_FILENO));

  p = job->process_list;
  while (p) {
    if (p->suspended){
      is_suspended = 1;
      break;
    }
    p = p->next;
  }
  if (is_suspended) {
    printf("job %d suspended\n", job->job_num);
  } else {
    printf("DELETE JOB: %s\n", job->cmd);
    delete_job(job->job_num);
  }
}


void launch_process(Process *p, int in, int out, pid_t pgid) {
  int n;
  pid_t pid;

  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);


  for(n = 0; p->arguments[n] != NULL; n++){
    printf("\t\targ %d: %s\n", n, p->arguments[n]);
  }
    // Put the process into the process group and give the process group the terminal
    // Has to be done by the shell and in the child processes
  pid = getpid();

  printf("PID: %d\n", pid);
  printf("PGID: %d\n", pgid);

  if (setpgid(pid, pgid) < 0) {
    printf("Error: cannot put child process %d into group %d\n", pid, pgid);
    exit(1);
  }

  tcsetpgrp(STDIN_FILENO, pgid);
  printf("foreground process group %d\n", tcgetpgrp(STDIN_FILENO));


  if (in != STDIN_FILENO)
    {
        dup2(in, STDIN_FILENO);
        close(in);
    }
    if (out != STDOUT_FILENO)
    {
        dup2(out, STDOUT_FILENO);
        close(out);
    }

  printf("\n\n\nLaunching process!\n");
  execvp(p->cmd, p->arguments);
  fprintf(stderr, "COMMAND NOT FOUND\n");
  exit(-1);
}

void wait_for_job(Job *job) {
  pid_t pid;
  int status;
  Process *p;
  do {
    for (p = job->process_list; p; p = p->next) {
      printf("\t\t\t\twait for process %d\n", p->pid);
      if (p->completed)
        continue;
      pid = waitpid(p->pid, &status, WUNTRACED);
      perror("waitpid");
      printf("WAIT PID %d\n", pid);
      if (pid < 0)
        return;
      
      update_process (job, pid, status);
    }
  } while(!job_is_completed(job) && !job_is_suspended(job));
}

int update_process(Job *job, pid_t pid, int status) {
  Process *p;
  for (p = job->process_list; p; p = p->next)
    if (p->pid == pid) {
      p->status = status;
      if (WIFSTOPPED(status)) {
        p->suspended = 1;
        printf("\tSuspended\n");
      }
      else {
        p->completed = 1;
        printf("\tPID %d completed\n", pid);
        p->status = WEXITSTATUS(status);
      }
      return 0;
    }
  fprintf (stderr, "No child process %d.\n", pid);
  return -1;  
}

int job_is_completed(Job* job) {
  Process* p = job->process_list;
  while (p) {
    printf("\tCompleted? %d\n", p->completed);    
    if (!p->completed)
      return 0;
    p = p->next;
  }
  return 1;
}

int job_is_suspended(Job* job) {
  Process* p = job->process_list;
  while (p) {
    printf("\tSuspended? %d\n", p->suspended);    
    if (!p->suspended && !p->completed)
      return 0;
    p = p->next;
  }
  return 1;
}

void reset_status(Job *j) {
  Process *p = j->process_list;
  while (p) {
    p->suspended = 0;
    p = p->next;
  }
}

void my_cd() {
  if (process_list->arg_count == 2) {
    if (chdir(process_list->arguments[1]) == -1)
      fprintf(stderr, "%s: cannot change directory\n", process_list->arguments[1]);
  } else {
    fprintf(stderr, "cd: wrong number of arguments\n");
  }
}

void my_exit() {
  if (job_list) {
    printf("There is at least one suspended job\n");
  } else {
    if (process_list->arg_count != 1) {
      fprintf(stderr, "exit: wrong number of arguments\n");
    } else {
      exit(EXIT_SUCCESS);
    }
  }
}

void my_jobs() {
  Job *j = job_list;
  if (j == NULL) 
    printf("No suspended job\n");
  while (j != NULL) {
    printf("[%d] %s\n", j->job_num, j->cmd);
    j = j->next;
  }
}

void my_fg() {
  if (process_list->arg_count != 2) {
    fprintf(stderr, "exit: wrong number of arguments\n");
  } else {
    Job *j;
    int job_num = atoi(process_list->arguments[1]); 
    j = find_job(job_num);
    if (j) {
      printf("Job wake up: %s\n", j->cmd);
      reset_status(j);
      put_job_in_fg(j, 1);
    } else {
      printf("fg: no such job\n");
    }
  }
}

void my_bg() {
  ;
}
// built-in commands include: cd, exit
// return 1 if built-in, 0 otherwise
int builtInCommands() {
  if (!strcmp("cd", process_list->cmd)) {
    my_cd();
    return 1;
  }
  if (!strcmp("jobs", process_list->cmd)) {
    printf("list job.\n");
    my_jobs();
    return 1;
  }
  if (!strcmp("fg", process_list->cmd)) {
    printf("put fg\n");
    my_fg();
    return 1;
  }
  if (!strcmp("bg", process_list->cmd)) {
    my_bg();
    return 1;
  }
  if (!strcmp("exit", process_list->cmd)) {
    my_exit();
  }

  return 0;
}

void handler() {
  if (process_list == NULL) {
    return;
  }
  if (!builtInCommands()) {
    //launch();
    Job* new_job = (Job *)malloc(sizeof(Job));
    init_job(new_job);
    printf("Handling Job %d: %s\n", new_job->job_num, new_job->cmd);

    launch_job(new_job);
  }
}

void printarg() {
  Process *p = process_list;
  while(p != NULL){
    if (p->type == BUILTIN)
      printf("This is built-in: %s\n", p->cmd);
    else if (p->type == NONBUILTIN)
      printf("This is not built-in: %s\n", p->cmd);
    else if (p->type == PIPE) 
      printf("\nThis is a pipe\n\n");
    if (p->type != PIPE)
      printf("Number of arguments: %d\n", p->arg_count);
    p = p->next;
  }
  printf("\n");
}

void prompt() {
  getcwd(current_dir, PATH_MAX+1);
  printf("[3150 shell:%s]$ ", current_dir);
}

void getLine() {
  getline(&line, &lineSize, stdin);

  // EOF
  if (feof(stdin)) {
    clearerr(stdin);
    printf("\n");
    exit(EXIT_SUCCESS);
  }

  line[strlen(line)-1] = '\0';

  split_line();
}

void cleanup(Process *p) {
  while (p->arg_count) {
    free(p->arguments[p->arg_count]);
    p->arg_count--;
  }
  free(p); 
}

void init_shell () {
  shell_terminal = STDIN_FILENO;
  shell_is_interactive = isatty (shell_terminal);

  // loop until in the foreground
  while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
    kill (- shell_pgid, SIGTTIN);

    // set environment
    setenv ("PATH","/bin:/usr/bin:.",1);    

    // ignore signal
    signal (SIGINT, SIG_IGN);
    signal (SIGQUIT, SIG_IGN);
    signal (SIGTSTP, SIG_IGN);
    signal (SIGTTIN, SIG_IGN);
    signal (SIGTTOU, SIG_IGN);
    signal (SIGTERM, SIG_IGN);

    // signal (SIGCHLD, SIG_IGN);

    shell_pgid = getpid();
    printf("Shell pgid: %d\n", shell_pgid);
    if (setpgid(shell_pgid, shell_pgid) < 0) {
      perror ("Couldn't put the shell in its own process group");
      exit(1);
    }
    // Grab control of the terminal
    tcsetpgrp(shell_terminal, shell_pgid);

    // Save default
    // tcgetattr(shell_terminal, &shell_tmodes);
}

int main(int argc, char **argv){

  init_shell();

  while (state) {
    prompt();
    getLine();
    printarg();
    handler();


    process_list = NULL;
    free(line);
    line = NULL;
  } 
  return 0;
}
