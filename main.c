#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>

char current_dir[PATH_MAX+1];
char *line;
size_t lineSize;
char **args;
size_t numtokens;
int state = 1;


int builtInCommands();
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
  args = calloc(tokens_alloc, sizeof(char*));

  char *token;

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

  pid = fork();

  if (pid == 0) {
    // set the signal handling back to default
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);

    if (execvp(args[0], args) == -1)
      perror("Unknown error");
    exit(-1);
  } else if (pid < 0){
    perror("Unknown error");
  } else {
    wpid = waitpid(pid, &status, WUNTRACED);
  };
}

void my_cd() {
  if (args[1] && !args[2]) {
    if (chdir(args[1]) == -1)
      printf("[%s]: cannot change directory\n", args[1]);
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
  if (builtInCommands() == 0) {
    setenv("PATH","/bin:/usr/bin:.",1);    
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
  if (getline(&line, &lineSize, stdin) == -1) {
    printf("\n");
    exit(EXIT_SUCCESS);
  }
  line[strlen(line)-1] = '\0';
  splitLine();
}

int main(int argc, char **argv){

  signal(SIGINT,SIG_IGN); 
  signal(SIGQUIT,SIG_IGN); 
  signal(SIGTERM,SIG_IGN); 
  signal(SIGTSTP,SIG_IGN);

  do {
    prompt();
    getLine();
   
    printArgs();
    handler();

    if (args != NULL)
      free(args);
    if (line != NULL)
      line = NULL;

  } while (state);

  return 0;
}
