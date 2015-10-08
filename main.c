#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>

char current_dir[PATH_MAX+1];
char *line;
size_t lineSize;
char **args;
size_t numtokens;
int state = 1;


int builtInCommands();
void cleanup();
void handler();
void launch();
void my_cd();
void my_exit();
void printArgs();
void prompt();
void splitLine();


void splitLine() {
  size_t tokens_alloc = 1;
  size_t tokens_used = 0;
  char *token;

  args = calloc(tokens_alloc, sizeof(char*));
  token = strtok(line, " ");
  while (token != NULL) {
    if (tokens_used == tokens_alloc) {
      tokens_alloc *= 2;
      args = realloc(args, tokens_alloc * sizeof(char*));
    }
    args[tokens_used++] = token;
    token = strtok(NULL, " ");
  }

  if (tokens_used == 0){
    free(args);
    args = NULL;
  } else {
    args[tokens_used] = NULL;
  }
  numtokens = tokens_used;
}

void launch() {
  pid_t pid, wpid;
  int status;

  if ((pid = fork()) == 0) {
    // set the signal handling back to default
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);

    if (execvp(args[0], args) == -1) {
      if (errno == 2) {
	printf("%s: command not found\n", args[0]);
      } else {
	printf("%s: unknown error\n", args[0]);
      }
    }
    exit(-1);
  } else if (pid < 0){
    fprintf(stderr, "Unknown error");
  } else {
    wpid = waitpid(pid, &status, WUNTRACED);
  };
}

void my_cd() {
  if (args[1] && !args[2]) {
    if (chdir(args[1]) == -1)
      fprintf(stderr, "%s: cannot change directory\n", args[1]);
  } else {
    fprintf(stderr, "cd: wrong number of arguments\n");
  }
}

void my_exit() {
  if (args[1] != NULL) {
    fprintf(stderr, "exit: wrong number of arguments");
  } else {
    exit(EXIT_SUCCESS);
  }
}


// built-in commands include: cd, exit
// return 1 if built-in, 0 otherwise
int builtInCommands() {
  if (!strcmp("cd", args[0])) {
    my_cd();
    return 1;
  }
  if (!strcmp("exit", args[0])) {
    my_exit();
  }
  return 0;
}

void handler() {
  if (args == NULL) {
    return;
  }
  if (!builtInCommands()) {
    launch();
  }
}

void printArgs() {
  size_t i;
  for (i = 0; i < numtokens; i++){
    printf("\ttokens: \"%s\"\n", args[i]);
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
    printf("\n");
    exit(EXIT_SUCCESS);
  }
  line[strlen(line)-1] = '\0';
  splitLine();
}

void cleanup() {
  while (numtokens) {
    args[numtokens--] = NULL;
  }
  free(args);
  line = NULL;
}

int main(int argc, char **argv){

  signal(SIGINT,SIG_IGN); 
  signal(SIGQUIT,SIG_IGN); 
  signal(SIGTERM,SIG_IGN); 
  signal(SIGTSTP,SIG_IGN);

  setenv ("PATH","/bin:/usr/bin:.",1);    

  while (state) {
    prompt();

    getLine();
    printArgs();
    handler();

    cleanup();
  } 
  return 0;
}
