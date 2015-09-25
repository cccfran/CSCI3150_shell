#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>

char **split_line(char* line, size_t* numtokens);

#define TOK_SEP " \n"
char **split_line(char *line, size_t *numtokens) {
  size_t tokens_alloc = 1;
  size_t tokens_used = 0;
  char **tokens = calloc(tokens_alloc, sizeof(char*));

  char *token;

  token = strtok(line, TOK_SEP);
  while (token != NULL) {
    if (tokens_used == tokens_alloc) {
      tokens_alloc *= 2;
      tokens = realloc(tokens, tokens_alloc * sizeof(char*));
    }
    tokens[tokens_used++] = token;

    token = strtok(NULL, TOK_SEP);
  }

  if (tokens_used == 0){
    free(tokens);
    tokens = NULL;
  } else {
    tokens[tokens_used] = NULL;
  }
  *numtokens = tokens_used;

  return tokens;
}

int launch(char **args) {
  pid_t pid, wpid;
  int status;

  pid = fork();

  if (pid == 0) {
    if (execvp(args[0], args) == -1)
      perror("Unknown error");
    exit(-1);
  } else if (pid < 0){
    perror("Unknown error");
  } else {
    wpid = waitpid(pid, &status, WUNTRACED);
  };
  return 1;
}

int my_cd(char **args) {
  if (!args[1] || args[2]) {
    fprintf(stderr, "cd: wrong number of arguments\n");
  } else {
    if (chdir(args[1]) == -1)
      printf("[%s]: cannot change directory\n", args[1]);
  }
  return 1;
}

int my_exit(char **args) {
  if (args[1] != NULL) {
    fprintf(stderr, "exit: wrong number of arguments");
    return 1;
  } else {
    return 0;
  }
}

char *command[] = {
  "cd",
  "exit"
};

int num_commands() {
  return sizeof(command) / sizeof(char *);
}

int (*function[]) (char **) = {
  &my_cd,
  &my_exit
};

int execute(char **args) {
  int i;

  if (args[0] == NULL) 
    return 1;

  setenv("PATH","/bin:/usr/bin:.",1);

  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);

  for (i = 0; i < num_commands(); i++) {
    if (strcmp(args[0], command[i]) == 0) {
      return (*function[i])(args);
    }
  }

  return launch(args);
}

void loop(void){
  char current_dir[PATH_MAX+1];
  char *line = NULL;
  size_t size;

  char **args;
  size_t numtokens;

  int status;

  signal(SIGINT,SIG_IGN); 
  signal(SIGQUIT,SIG_IGN); 
  signal(SIGTERM,SIG_IGN); 
  signal(SIGTSTP,SIG_IGN);

  do {
    getcwd(current_dir, PATH_MAX+1);
    printf("[3150 shell:%s]$ ", current_dir);
    if (getline(&line, &size, stdin) == -1){
      printf("\n");
      break;
    }
    line[strlen(line)-1] = '\0';
    args = split_line(line, &numtokens);
    size_t i;
    for (i = 0; i < numtokens; i++){
      printf("\ttokens: \"%s\"\n", args[i]);
    }
    
    if (args == NULL) {			   
      continue;
    }
    status = execute(args);

    if (args != NULL)
      free(args);
    if (line != NULL)
      line = NULL;

  } while (status);
}

int main(int argc, char **argv) {
  loop();
  return 0;
}
