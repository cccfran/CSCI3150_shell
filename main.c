#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>

#define SIGDEFAULT 1
#define SIGIGNORE 0
#define CMDSIZE 256
#define MAXPIPE 64

#define PIPE 1
#define BUILTIN 2
#define NONBUILTIN 3

typedef struct tokens {
  int type;
  int argCount;
  pid_t pid;
  char cmd[CMDSIZE];
  char *arguments[CMDSIZE];
  struct tokens* next;
} Tokens;

char current_dir[PATH_MAX+1];
char *line;
size_t lineSize;
char *arg[256];
char **args;
size_t numtokens;
int numpipes;
int state = 1;
Tokens *tokenList = NULL;
int pipes[MAXPIPE][2];

int builtInCommands();
void cleanup();
void initProcess(Tokens *token, int const pipes[][2], int pipenum);
void handler();
void InitCommand(Tokens *command);
void launch();
void my_bg();
void my_cd();
void my_exit();
void my_fg();
void my_jobs();
void printarg();
void prompt();
void setSig(int defaultFlag);
void splitLine();

void splitLine() {
  char *token;
  Tokens *cptr = NULL, *ctmp;
  token = strtok(line, " ");
  int commandflag = 1;
  numtokens = 0;
  numpipes = 0;

  while(token != NULL) {
    if (strlen(token) == 1 && token[0] == '|') {
      commandflag = 1;
      numpipes++;
    }
    if (commandflag) {
      printf("cmd: %s\n", token);
      ctmp = cptr;
      cptr = (Tokens *)malloc(sizeof(Tokens));
      if (ctmp)
        ctmp->next = cptr;
      InitCommand(cptr);
      strcpy(cptr->cmd, token);
      cptr->arguments[cptr->argCount] = (char*)malloc(sizeof(char) * (strlen(token) + 1));
      strcpy(cptr->arguments[cptr->argCount++], token);

      if ( !strcmp(cptr->cmd, "cd") || !strcmp(cptr->cmd, "exit") \
        || !strcmp(cptr->cmd, "jobs") || !strcmp(cptr->cmd, "fg"))
        cptr->type = BUILTIN;
      else if (!strcmp(cptr->cmd, "|"))
        cptr->type = PIPE;
      else
        cptr->type = NONBUILTIN;

      if (cptr->type != PIPE)
        commandflag = 0;  
    } else {
      printf("args: %s\n", token);
      cptr->arguments[cptr->argCount] = (char*)malloc(sizeof(char) * (strlen(token) + 1));
      strcpy(cptr->arguments[cptr->argCount++], token);
    }
    numtokens++;
    if (numtokens == 1)
      tokenList = cptr;

    token = strtok(NULL, " ");
  }

  Tokens *tmp;
  ctmp = tokenList;
  while(ctmp != NULL && ctmp->next != NULL) {
    if (ctmp->next->type == PIPE) {
      tmp = ctmp->next;
      ctmp->next = tmp->next;
      cleanup(tmp);
    }
    ctmp = ctmp->next;
  }
  printf("Number of pipes: %d\n", numpipes);
}

void InitCommand(Tokens *cptr){
  cptr->type = 0;
  cptr->argCount = 0;
  cptr->next = NULL;
  cptr->pid = 0;
}
/***
void splitLine() {
  
  size_t tokens_alloc = 1;
  size_t tokens_used = 0;
  char *token;

  arg = calloc(tokens_alloc, sizeof(char*));
  token = strtok(line, " ");
  while (token != NULL) {
    if (tokens_used == tokens_alloc) {
      tokens_alloc *= 2;
      arg = realloc(arg, tokens_alloc * sizeof(char*));
    }
    arg[tokens_used++] = token;
    token = strtok(NULL, " ");
  }

  if (tokens_used == 0){
    free(arg);
    arg = NULL;
  } else {
    arg[tokens_used] = NULL;
  }
  numtokens = tokens_used;
  
  size_t args_alloc = 1;
  size_t args_used = 0;
  args = calloc(tokens_alloc, sizeof(char*));
  arg = strtok(line, "|");
  while (arg != NULL) {
    if (args_used == args_alloc) {
      tokens_alloc *= 2;
      args = realloc(args, tokens_alloc * sizeof(char*));
    }
    args[tokens_used++] = arg;
    arg = strtok(NULL, "|");
  }
  numpipes = args_used - 1;

  char *token;
  size_t tokens_used = 0;
  token = strtok(line, " ");
  while (token != NULL) {
    arg[tokens_used] = token;
    token = strtok(NULL, " ");
  }
  if (tokens_used == 0) {
    arg = NULL;
  } else {
    arg[tokens_used] = NULL;
  }
  numtokens = tokens_used;
}
***/

void launch() {
  int m;

  for (m = 0; m < MAXPIPE; m++) {
    pipes[m][STDIN_FILENO] = -1;
    pipes[m][STDOUT_FILENO] = -1;
  }

  for (m = 0; m < numpipes; m++) {
    int p[2];
    pipe(p);
    pipes[m][STDOUT_FILENO] = p[STDOUT_FILENO];
    pipes[m+1][STDIN_FILENO] = p[STDIN_FILENO];
  }

  Tokens *tmp;
  tmp = tokenList;
  for (m = 0; m <= numpipes; m++) {
    fprintf(stderr, "\ttoken %d: %s\n", m, tmp->cmd);
    initProcess(tmp, pipes, m);
    tmp = tmp->next;
  }
// Close all pipes  
  for (m = 0; m < MAXPIPE; m++) {
    if(pipes[m][STDIN_FILENO] >= 0) close(pipes[m][STDIN_FILENO]);
    if(pipes[m][STDOUT_FILENO] >= 0) close(pipes[m][STDOUT_FILENO]);
  }

  tmp = tokenList;
  for (m = 0; m <= numpipes; m++) {
    int stat;
    waitpid(tmp->pid, &stat, 0);
    fprintf(stderr, "Child %d returned %d\n", m, WEXITSTATUS(stat));
    tmp = tmp->next;
  }  


/***
  pid_t pid, wpid;
  int status;

  if ((pid = fork()) == 0) {
    // set the signal handling back to default
    setSig(SIGDEFAULT);

    if (execvp(arg[0], arg) == -1) {
      if (errno == 2) {
	printf("%s: command not found\n", arg[0]);
      } else {
	printf("%s: unknown error\n", arg[0]);
      }
    }
    exit(-1);
  } else if (pid < 0){
    fprintf(stderr, "Unknown error");
  } else {
    wpid = waitpid(pid, &status, WUNTRACED);
  };
  ***/
}

void initProcess(Tokens *token, int const pipes[][2], int pipenum) {
    pid_t pid;

    int n;
    for(n=0; token->arguments[n] != NULL; n++){
      printf("\t\targ %d: %s\n", n, token->arguments[n]);
    }

    pid = fork();

    if(pid == 0) {
      setSig(SIGDEFAULT);

      int m;

      if(pipes[pipenum][STDIN_FILENO] >= 0) 
        dup2(pipes[pipenum][STDIN_FILENO], STDIN_FILENO); // FD 0
      if(pipes[pipenum][STDOUT_FILENO] >= 0) 
        dup2(pipes[pipenum][STDOUT_FILENO], STDOUT_FILENO); // FD 1

      // Close all pipes
      for(m=0; m<64; m++)
      {
          if(pipes[m][STDIN_FILENO] >= 0) 
            close(pipes[m][STDIN_FILENO]);
          if(pipes[m][STDOUT_FILENO] >= 0) 
            close(pipes[m][STDOUT_FILENO]);
      }

      execvp(token->cmd, token->arguments);
      fprintf(stderr, "COMMAND NOT FOUND\n");
      exit(255);
    }
    token->pid = pid;
}

void my_cd() {
  if (tokenList->argCount == 2) {
    if (chdir(tokenList->arguments[1]) == -1)
      fprintf(stderr, "%s: cannot change directory\n", tokenList->arguments[1]);
  } else {
    fprintf(stderr, "cd: wrong number of arguments\n");
  }
}

void my_exit() {
  if (tokenList->argCount != 1) {
    fprintf(stderr, "exit: wrong number of arguments");
  } else {
    exit(EXIT_SUCCESS);
  }
}

void my_jobs() {
  ;
}
void my_fg() {
  ;
}
void my_bg() {
  ;
}
// built-in commands include: cd, exit
// return 1 if built-in, 0 otherwise
int builtInCommands() {
  if (!strcmp("cd", tokenList->cmd)) {
    my_cd();
    return 1;
  }
  if (!strcmp("jobs", tokenList->cmd)) {
    my_jobs();
    return 1;
  }
  if (!strcmp("fg", tokenList->cmd)) {
    my_fg();
    return 1;
  }
  if (!strcmp("bg", tokenList->cmd)) {
    my_bg();
    return 1;
  }
  if (!strcmp("exit", tokenList->cmd)) {
    my_exit();
  }

  return 0;
}

void handler() {
  if (tokenList == NULL) {
    return;
  }
  if (!builtInCommands()) {
    launch();
  }
}

void printarg() {
  Tokens *p = tokenList;
  while(p != NULL){
    if (p->type == BUILTIN)
      printf("This is built-in: %s\n", p->cmd);
    else if (p->type == NONBUILTIN)
      printf("This is not built-in: %s\n", p->cmd);
    else if (p->type == PIPE)
      printf("This is a pipe\n\n");
    printf("Number of arguments: %d\n", p->argCount);
    p = p->next;
  }
}

void prompt() {
  getcwd(current_dir, PATH_MAX+1);
  printf("[3150 shell:%s]$ ", current_dir);
}

void getLine() {
  /***
  if (getline(&line, &lineSize, stdin) == -1) {
    printf("\n");
    exit(EXIT_SUCCESS);
  }
  ***/
  getline(&line, &lineSize, stdin);
  if (feof(stdin)) {
    clearerr(stdin);
    printf("\n");
    exit(EXIT_SUCCESS);
  }
  line[strlen(line)-1] = '\0';
  splitLine();
}

void cleanup(Tokens *tokens) {
  while (tokens->argCount) {
    free(tokens->arguments[tokens->argCount]);
    tokens->argCount--;
  }
  free(tokens); 
}

void setSig(int defaultFlag) {
  if (defaultFlag) {
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
  } else {
    signal(SIGINT,SIG_IGN); 
    signal(SIGQUIT,SIG_IGN);   
    signal(SIGTERM,SIG_IGN); 
    signal(SIGTSTP,SIG_IGN);
  }
}

int main(int argc, char **argv){

  setSig(SIGIGNORE);

  setenv ("PATH","/bin:/usr/bin:.",1);    

  while (state) {
    prompt();

    getLine();
    printarg();
    
    handler();
/*
    cleanup();
    */
  } 
  return 0;
}
