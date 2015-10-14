#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <glob.h>

#define INPUT_MAX 257

//Define cutom structures
typedef struct invocation_s inv;
typedef struct job_s job;

struct job_s {
    int job_count;
    char cmdinput[INPUT_MAX]; //For printing cmd
    char pipeline[INPUT_MAX]; //For inv args ptr
    int inv_num;
    inv *invs[INPUT_MAX/4];
    job *next_job;
    job *prev_job;
};

struct invocation_s {
    int arg_num;
    char *args[INPUT_MAX/2];
    pid_t process_id;
};

//Define global variables
char cwd[PATH_MAX];
char input[INPUT_MAX];
char* job_cmd;
job* job_new;
job* job_list;
int init_job_count;
glob_t wildcards;
short wc_flag;
int wc_count;

//Declare functions for custom structures
job* init_job(char *input);
void free_job(job* job_node);
job* last_job(void);
void add_job(job* job_node);
void delete_job(job *job_node);
job* get_job_by_pid(pid_t job_pid);
job* get_job_by_fg(int fg_num);
void print_job_list(void);
void print_job_list_reverse(void);
void print_job(job* job_node);
inv* init_inv(void);
void free_inv(inv* inv_node);

//Declare functions for shell
//For first read of command
void init_shell(void);
void getinput(void);
short create_job(void);
void parse(void);
void add_wildcards(char* pattern);
void execute(void);
void exe_cd(void);
void exe_exit(void);
void exe_jobs(void);
void exe_fg(void);
void exe_single(void);
void exe_pipe(void);
void wait_child_processes(job* job_node);

//Functions for structure job
job* init_job(char *input){
    job* tmp = (job*)malloc(sizeof(job));
    strcpy(tmp->cmdinput, input);
    strcpy(tmp->pipeline, input);
    tmp->job_count = ++init_job_count;
    tmp->inv_num = 0;
    tmp->next_job = NULL;
    tmp->prev_job = NULL;
    return tmp;
}

job* init_job_list(){
    job* tmp = (job*)malloc(sizeof(job));
    strcpy(tmp->cmdinput, "list head");
    tmp->job_count = init_job_count;
    tmp->inv_num = 0;
    tmp->next_job = NULL;
    tmp->prev_job = NULL;
    return tmp;
}

void free_job(job* job_node){
    int i;
    for (i = 0; i < job_node->inv_num; i++)
        free_inv(job_node->invs[i]);
    free(job_node);
}

job* last_job(void){
    job* job_node = job_list;
    while (job_node->next_job)
        job_node = job_node->next_job;
    return job_node;
}

void add_job(job* job_node){
    job* tmp;
    for (tmp = job_list; tmp->next_job; tmp = tmp->next_job){
        if (job_node->job_count < tmp->next_job->job_count){
            job_node->prev_job = tmp;
            job_node->next_job = tmp->next_job; 
            tmp->next_job->prev_job = job_node;
            tmp->next_job = job_node;
            break;
        }
    }
    //if job_node is newer than all job in list, i.e. tmp is the last job
    tmp->next_job = job_node;
    job_node->prev_job = tmp;
}

void delete_job(job *job_node){
    print_job_list_reverse();
    if (job_node->next_job){
        job_node->next_job->prev_job = job_node->prev_job;
        job_node->prev_job->next_job = job_node->next_job;
    }else {
        job_node->prev_job->next_job = NULL;
    }
    job_node->next_job = NULL;
    job_node->prev_job = NULL;
}

job* get_job_by_pid(pid_t job_pid){
    job* tmp = job_list->next_job;
    while(tmp){
        int i;
        for (i = 0; i < tmp->inv_num; i++){
            if (tmp->invs[i]->process_id == job_pid)
                return tmp;
        }
        tmp = tmp->next_job;
    }
    return NULL;
}

job* get_job_by_fg(int fg_num){
    job* tmp = job_list;
    int i;
    for (i = 0; i < fg_num; i++){
        if (i == (fg_num)-1 && tmp->next_job)
            tmp = tmp->next_job;
        else
            return NULL;
    }
    return tmp;
}
void print_job(job* job_node){
    int i, j;
    printf("inv number: %d\n", job_node->inv_num);
    for (i = 0; i < job_node->inv_num ; i++){
        printf("arg num: %d\n", job_node->invs[i]->arg_num);
        for (j = 0; j < job_node->invs[i]->arg_num; j++)
            printf("%s\t", job_node->invs[i]->args[j]);
        printf("NULL\n");
    }
}

void print_job_list(void){
    job* job_node = job_list->next_job;
    int job_count;
    for (job_count = 1; job_node; job_node = job_node->next_job, job_count++)
        printf("[%d] %s\n", job_count, job_node->cmdinput);
}
void print_job_list_reverse(void){
    job* job_node = last_job();
    int job_count;
    for (job_count = 0; job_node; job_node = job_node->prev_job, job_count++)
        printf("[%d] %s\n", job_count, job_node->cmdinput);
}

//Functions for structure inv
inv* init_inv(void){
    inv *tmp = (inv*)malloc(sizeof(inv));
    tmp->arg_num = 0;
    return tmp;
}

void free_inv(inv* inv_node){
    free(inv_node);
}

//Shell Functions
void init_shell(void){
    setenv("PATH", "/bin:/usr/bin:.", 1);
    signal(SIGINT,SIG_IGN);
    //signal(SIGQUIT,SIG_IGN);
    signal(SIGTERM,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);

    job_list = init_job_list();

    wc_flag = 0;
    wc_count = 0;
    init_job_count = 0;
}
void getinput(void){
    getcwd(cwd, PATH_MAX);
    printf("[3150 shell:%s]$ ", cwd);
    if (!fgets(input, INPUT_MAX, stdin)) {
        printf("\n");
        exit(EXIT_SUCCESS);
    }
    input[strlen(input)-1] = '\0';
}

short create_job(void){
    job_new = init_job(input);
    job_cmd = job_new->pipeline;
    return 0;
}

void parse(void){
    job_new->invs[0] = init_inv();
    int *inv_count = &(job_new->inv_num),
        *arg_count = &(job_new->invs[0]->arg_num);

    char *tok = strtok(job_cmd, " ");
    //printf("tok: %s, ", tok);
    //printf("inv_c: %d, arg_c: %d (nor)\n", *inv_count, *arg_count);
    job_new->invs[*inv_count]->args[*arg_count] = tok;
    (*arg_count)++;

    while(tok != NULL){
        tok = strtok(NULL, " ");
        //printf("tok: %s, ", tok);
        if (!tok)
        {
            //printf("inv_c: %d, arg_c: %d (null)\n", *inv_count, *arg_count);
            job_new->invs[*inv_count]->args[*arg_count] = NULL;
        }
        else if (strcmp(tok, "|")){
            if (strstr(tok, "*"))
            {
                //printf("expand wildcard\n");
                add_wildcards(tok);
                for (wc_count; wc_count < wildcards.gl_pathc; wc_count++){
                    //printf("\ttok: %s, inv_c: %d, arg_c: %d (wc)\n", wildcards.gl_pathv[wc_count], *inv_count, *arg_count);
                    job_new->invs[*inv_count]->args[(*arg_count)++] = wildcards.gl_pathv[wc_count];
                }
            }
            else {
                //printf("inv_c: %d, arg_c: %d (nor)\n", *inv_count, *arg_count);
                job_new->invs[*inv_count]->args[(*arg_count)++] = tok;
            }
        } else {
            //printf("inv_c: %d, arg_c: %d (pipe)\n", *inv_count, *arg_count);
            job_new->invs[*inv_count]->args[*arg_count] = NULL;
            (*inv_count)++;
            job_new->invs[*inv_count] = init_inv();
            arg_count = &(job_new->invs[*inv_count]->arg_num);
            //printf("----- pipe: inv_c=%d, arg_c=%d\n", *inv_count, *arg_count);
        }
    }
    (*inv_count)++;
}

void add_wildcards(char *pattern){
    if (wc_flag)
        glob(pattern, GLOB_NOCHECK | GLOB_APPEND, NULL, &wildcards);
    else {
        glob(pattern, GLOB_NOCHECK, NULL, &wildcards);
        wc_flag = 1;
    }
}

void execute(void){
    char* first_tok = job_new->invs[0]->args[0];
    if (!strcmp(first_tok, "cd"))
        exe_cd();
    else if (!strcmp(first_tok, "exit"))
        exe_exit();
    else if (!strcmp(first_tok, "jobs"))
        exe_jobs();
    else if (!strcmp(first_tok, "fg"))
        exe_fg();
    else if (job_new->inv_num == 1)
        exe_single();
    else
        exe_pipe();
}

void exe_cd(void){
    if (job_new->inv_num != 1 || job_new->invs[0]->arg_num != 2)
        printf("cd: wrong number of arguments\n");
    else {
        if (chdir(job_new->invs[0]->args[1]) == -1){
            printf("%s: cannot change directory\n", job_new->invs[0]->args[1]);
            //printf("Error is %d, [%s]\n", errno, strerror(errno));
        }
    }
}

void exe_exit(void){
    if (job_new->inv_num != 1 || job_new->invs[0]->arg_num != 1)
        printf("exit: wrong number of arguments\n");
    else if (job_list->next_job)
        printf("There is at least one suspended job\n");
    else
        exit(EXIT_SUCCESS);
}

void exe_jobs(void){
    if (job_new->inv_num != 1 || job_new->invs[0]->arg_num != 1)
        printf("jobs: wrong number of arguments\n");
    else if (job_list->next_job)
        print_job_list();
    else
        printf("No suspended jobs\n");
        
}

void exe_fg(void){
    if (job_new->inv_num != 1 || job_new->invs[0]->arg_num != 2)
        printf("fg: wrong number of arguments\n");
    else {
        int fg_num = (int)strtol(job_new->invs[0]->args[1], NULL, 10);
        job* job_node = get_job_by_fg(fg_num);
        if (job_node){
            printf("before delete job_node from list\n");
            delete_job(job_node);
            int i;
            printf("job_node: ");
            print_job(job_node);
            for (i = 0; i < job_node->inv_num; i++){
                pid_t wake_pid = job_node->invs[i]->process_id;
                printf("sending signal to process %d\n", wake_pid);
                kill(wake_pid, SIGCONT);
            }
            printf("all signal sent\n");
            wait_child_processes(job_node);
        } else
            printf("fg: no such job\n");
    }
}

void exe_single(void){
    char **args = job_new->invs[0]->args;
    pid_t fork_pid = fork();

    if (fork_pid == 0){
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
    } else if (fork_pid == -1)
        printf("%s: unknown error\n", args[0]);
    else
        wait_child_processes(job_new);
}

void exe_pipe(void){
    pid_t fork_pid;
    char **args;
    int inv_c = job_new->inv_num;
    int pipefd[2], in = STDIN_FILENO, out;
    int i;
    for (i = 0; i < inv_c ; i++){
        if (i == inv_c - 1){
            pipe(pipefd);
            out = pipefd[STDOUT_FILENO];
        } else
            out = STDOUT_FILENO;

        args = job_new->invs[i]->args;
        fork_pid = fork();
        if (fork_pid == 0){
            signal(SIGINT,SIG_DFL);
            signal(SIGQUIT,SIG_DFL);
            signal(SIGTERM,SIG_DFL);
            signal(SIGTSTP,SIG_DFL);

            if (in != STDIN_FILENO){
                dup2(in, STDIN_FILENO);
                close(in);
            }
            if (out != STDOUT_FILENO){
                dup2(out, STDOUT_FILENO);
                close(out);
            }

            if (execvp(*args, args) == -1) {
                if (errno == ENOENT)
                    printf("%s: command not found\n", args[0]);
                else
                    printf("%s: unknown error\n", args[0]);
                exit(EXIT_FAILURE);
            }
        } else if (fork_pid == -1)
            printf("%s: unknown error\n", args[0]);
        else
            job_new->invs[i]->process_id = getpid();

        if (in != STDIN_FILENO)
            close(in);
        if (out != STDOUT_FILENO)
            close(out);
        in = pipefd[STDIN_FILENO];
    }
    if (fork_pid > 0)
        wait_child_processes(job_new);
}

void wait_child_processes(job* job_node){
    int i;
    int status;
    for (i = 0; i < job_node->inv_num; i++){
        waitpid(job_node->invs[i]->process_id, &status, WUNTRACED);
        if (WIFSTOPPED(status)){
            printf("suspend signal detected\n");
            add_job(job_new);
            break;
        }
    }
    free_job(job_node);
}

int main(void){
    init_shell();
    while(1){
        getinput();
        if(!input)
            continue;
        create_job();
        parse();
        print_job(job_new);
        execute();
    }
    return 0;
}
