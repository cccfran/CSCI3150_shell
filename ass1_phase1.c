#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#define INPUT_MAX 257;

char cmdinput[INPUT_MAX];
char cwd[PATH_MAX];
int argNum;
char *args[INPUT_MAX/2];
pid_t shellPid;

void getinput(void){
    getcwd(cwd, PATH_MAX);
    printf("[3150 shell:%s]$ ", cwd);
    fgets(cmdinput, INPUT_MAX, stdin);
}

void parse(void){
    cmdinput[strlen(cmdinput)-1] = '\0';
    args[0] = strtok(cmdinput, " ");
    argNum = 0;
    while (args[argNum] != NULL){
        args[++argNum] = strtok(NULL, " ");
    }
//  int i;
//  printf("argNum = %d\n", argNum);
//  for (i = 0; i<argNum; i++)
//      printf("arg[%d]: %s\n", i, args[i]);
}

void exe_cd(void){
    if (argNum != 2) {
        printf("cd: wrong number of arguments\n");
    }
    else {
        if (chdir(args[1]) == -1){
            printf("%s: cannot change directory\n", args[1]);
            //printf("Error is %d, [%s]\n", errno, strerror(errno));
        }
    }
}

void exe_exit(void){
    if(argNum != 1)
        printf("exit: wrong number of arguments\n");
    else
        exit(EXIT_SUCCESS);
}

void exe_executables(void){
    pid_t child_pid = fork();

    if (child_pid == 0){
        signal(SIGINT,SIG_DFL);
        signal(SIGQUIT,SIG_DFL);
        signal(SIGTERM,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);

        if (execvp(*args, args) == -1) {
            if (errno == ENOENT)
                printf("%s: command not found\n", args[0]);
            else
                printf("%s: unknown error\n", args[0]);
            exit(EXIT_FAILURE);
        }
    } else if (child_pid == -1)
        printf("%s: unknown error\n", args[0]);
    else
        waitpid(child_pid, NULL, WUNTRACED);
}

void execute(void){
    if (argNum > 0) {
        if (!strcmp(args[0], "cd"))
            exe_cd();
        else if (!strcmp(args[0], "exit"))
            exe_exit();
        else
            exe_executables(); 
    }
}

int main (void){
    signal(SIGINT,SIG_IGN);
    signal(SIGQUIT,SIG_IGN);
    signal(SIGTERM,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);
    shellPid = getpid();

    setenv("PATH", "/bin:/usr/bin:.", 1);

    while (1) {
        getinput();
        parse();
        execute(); 
    }

    return 0;
}
