#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <glob.h>

#define INPUT_MAX 257

typedef struct inv_s{
    int arg_num;
    char **args;
    int args_capacity;
    pid_t process_id;
    short is_completed;
} inv;

typedef struct job_s{
    int timestamp;
    char jobname[INPUT_MAX]; //for job printing
    char pipeline[INPUT_MAX]; //for args ptr
    int inv_num;
    struct inv_s *invs[INPUT_MAX/4];
    struct job_s *next_job;
} job;

//Global variables
job* job_list;
int job_count;
glob_t wildcards;
short wc_flag;
int wc_count;
//Global variables for new command
char cwd[PATH_MAX];
char input[INPUT_MAX];
job* job_new;

//Functions for struct job
job* init_job(void);
job* init_job_node(char *job_cmd);
void add_job(job* job_node);
job* delete_and_get_job_by_fg(int fg_num);
void print_job_list(void);
void print_job_node(job* job_node);
//Functions for struct inv
inv* init_inv(void);

//Functions for shell
void init_shell(void);
void getinput(void);
void create_job(void);
void parse(void);
void check_args_mem(inv *inv_node, int expand_size);
void add_wildcards(char* pattern);
void execute(void);
void exe_cd(void);
void exe_exit(void);
void exe_jobs(void);
void exe_fg(void);
void exe_program(void);
void init_child_process(void);
void wait_child_processes(job* job_node);

job* init_job(void){
    job *tmp = (job*)malloc(sizeof(job));
    tmp->timestamp = job_count++;
    tmp->inv_num = 0;
    tmp->next_job = NULL;
    return tmp;
}

job* init_job_node(char *job_cmd){
    job *tmp = init_job();
    strcpy(tmp->jobname, job_cmd);
    strcpy(tmp->pipeline, job_cmd);
    //printf("*debug* init_job_node: job_cmd = %s\n", job_cmd); 
    //printf("*debug* init_job_node: tmp->jobname = %s\n", tmp->jobname);
    //printf("*debug* init_job_node: tmp->pipeline = %s\n", tmp->pipeline);
    return tmp;
}

void add_job(job* job_node){
    job* job_ptr = job_list;
    while (job_ptr->next_job){
        if (job_node->timestamp > job_ptr->next_job->timestamp)
            job_ptr = job_ptr->next_job;
        else
            break;
    }
    job_node->next_job = job_ptr->next_job;
    job_ptr->next_job = job_node;
}

job* delete_and_get_job_by_fg(int fg_num){
    //printf("*debug* delete_and_get_job_by_fg: entered\n");
    job *job_ptr = job_list, *job_node;
    int i;
    for (i = 1; i < fg_num; i++){
        if (job_ptr->next_job)
            job_ptr = job_ptr->next_job;
        else {
            //printf("*debug* delete_and_get_job_by_fg: fg_num too large\n");
            return NULL;
        }
    }
    if (!job_ptr->next_job)
        return NULL;
    job_node = job_ptr->next_job;
    job_ptr->next_job = job_node->next_job;
    //printf("*debug* delete_and_get_job_by_fg: leave\n");
    return job_node;
}

void print_job_list(void){
    job* job_node = job_list->next_job;
    int job_count;

    for (job_count = 1; job_node; job_count++){
        printf("[%d] %s\n", job_count, job_node->jobname);
        job_node = job_node->next_job;
    }
    if (job_count == 1)
        printf("No suspended jobs\n");
}

void print_job_node(job* job_node){
    printf("job cmd: %s; inv_num: %d; arg_num:", job_node->jobname, job_node->inv_num);
    int i;
    for (i=0; i < job_node->inv_num; i++)
        printf(" %d", job_node->invs[i]->arg_num);
    if (job_node->invs[i]->process_id != 1){
        printf("; process_id: ");
        for (i=0; i < job_node->inv_num; i++)
            printf(" %d", job_node->invs[i]->process_id);
    } else
        printf("; not yet executed");
    if (job_node->next_job)
        printf("; next_job: %s", job_node->next_job->jobname);
    else
        printf("; next_job: no next jobs");
    printf("\n");
}

inv* init_inv(void){
    inv* tmp = (inv*)malloc(sizeof(inv));
    tmp->arg_num = 0;
    tmp->args = (char**)malloc(sizeof(char*)*(INPUT_MAX/2));
    tmp->args_capacity = INPUT_MAX/2;
    tmp->process_id = 1;
    tmp->is_completed = 0;
    return tmp;
}

void init_shell(void){
    //printf("*debug* init_shell: entered\n");
    setenv("PATH", "/bin:/usr/bin:.", 1);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    //printf("*debug* init_shell: after change signal handler\n");

    job_list = init_job();
    //printf("*debug* init_shell: after init_job\n");
    job_count = 0;
    wc_flag = 0;
    wc_count = 0;
    //printf("*debug* init_shell: leave\n");
}

void getinput(void){
    getcwd(cwd, PATH_MAX);
    printf("[3150 shell:%s]$ ", cwd);
    if (!fgets(input, INPUT_MAX, stdin)){
        printf("\n");
        exit(EXIT_SUCCESS);
    }
    //printf("*debug* getinput: input = %s, input len = %d\n", input, strlen(input));
    input[strlen(input)-1] = '\0';
    //if (input)
    //    printf("*debug* getinput: input = %s, input len = %d\n", input, strlen(input));
    //else
    //    printf("*debug* getinput: input is null\n");
}

void create_job(void){
    //printf("*debug* create_job: input = %s\n", input);
    job_new = init_job_node(input);
}

void parse(void){
    //printf("*debug* parse: entered\n");
    char *job_cmd = job_new->pipeline;
    //printf("*debug* parse: job_new->pipeline = %s\n", job_new->pipeline);
    //printf("*debug* parse: job_cmd = %s\n", job_cmd);
    job_new->invs[0]= init_inv();
    int *inv_count = &(job_new->inv_num),
        *arg_count = &(job_new->invs[0]->arg_num);

    char *tok = strtok(job_cmd, " ");
    //printf("*debug* parse: get first tok, which is %s\n", tok);
    job_new->invs[*inv_count]->args[(*arg_count)++] = tok;

    while(tok != NULL){
        tok = strtok(NULL, " ");
        if (!tok)
            break;

        if (strcmp(tok, "|")){
            if (strstr(tok, "*")){
                add_wildcards(tok);
                check_args_mem(job_new->invs[*inv_count], (wildcards.gl_pathc - wc_count));
                for (; wc_count < wildcards.gl_pathc; wc_count++)
                    job_new->invs[*inv_count]->args[(*arg_count)++] = wildcards.gl_pathv[wc_count];
            } else{
                check_args_mem(job_new->invs[*inv_count], 1);
                job_new->invs[*inv_count]->args[(*arg_count)++] = tok;
            }
        } else {
            job_new->invs[(*inv_count)++]->args[*arg_count] = NULL;
            job_new->invs[*inv_count] = init_inv();
            arg_count = &(job_new->invs[*inv_count]->arg_num);
        }
    }
    job_new->invs[(*inv_count)++]->args[*arg_count] = NULL;
}

void check_args_mem(inv *inv_node, int expand_size){
    if (inv_node->arg_num + expand_size > inv_node->args_capacity){
        inv_node->args = realloc(inv_node->args, sizeof(char*)*(inv_node->arg_num + expand_size + 1));
        inv_node->args_capacity = inv_node->arg_num + expand_size + 1;
    }
}

void add_wildcards(char* pattern){
    if (wc_flag)
        glob(pattern, GLOB_NOCHECK | GLOB_APPEND, NULL, &wildcards);
    else {
        glob(pattern, GLOB_NOCHECK, NULL, &wildcards);
        wc_flag = 1;
    }
}

void execute(void){
    //printf("*debug* execute: entered\n");
    char *first_tok = job_new->invs[0]->args[0];
    //printf("*debug* execute: get first_tok, which is %s\n", first_tok);
    if (!strcmp(first_tok, "cd"))
        exe_cd();
    else if (!strcmp(first_tok, "exit"))
        exe_exit();
    else if (!strcmp(first_tok, "jobs"))
        exe_jobs();
    else if (!strcmp(first_tok, "fg"))
        exe_fg();
    else
        exe_program();
}

void exe_cd(void){
    //printf("*debug* cd: entered\n");
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
    //printf("*debug* exit: entered\n");
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
        char *endptr;
            
        int fg_num = (int)strtol(job_new->invs[0]->args[1], &endptr, 10);
        if (*endptr != '\0'){
            printf("fg: wrong arguments\n");
            return;
        }
        
        job* job_node = delete_and_get_job_by_fg(fg_num);
        if (job_node){
            int i;
            for (i = 0; i < job_node->inv_num; i++){
                pid_t wake_pid = job_node->invs[i]->process_id;
                kill(wake_pid, SIGCONT);
            }
            wait_child_processes(job_node);
        } else
            printf("fg: no such job\n");
    }
}

void exe_program(void){
    int pipefd[2];

    int in = STDIN_FILENO, out;
    char **args;
    pid_t fork_pid;

    int i;
    for (i = 0; i < job_new->inv_num; i++){
        if (job_new->inv_num - 1 - i == 0)
            out = STDOUT_FILENO;
        else{
            pipe(pipefd);
            out = pipefd[1];
        }

        args = job_new->invs[i]->args;
        fork_pid = fork();
        if (fork_pid == 0){
            init_child_process();

            if (in != STDIN_FILENO){ //Not first invocation
                dup2(in, STDIN_FILENO);
                close(in);
            }
            if (out != STDOUT_FILENO){ //Not last invocation
                dup2(out, STDOUT_FILENO);
                close(out);
                close(pipefd[0]);
            }

            if (execvp(*args, args) == -1) {
                if (errno == ENOENT)
                    fprintf(stderr, "%s: command not found\n", args[0]);
                else
                    printf("%s: unknown error\n", args[0]);
                exit(EXIT_FAILURE);
            }
        } else if (fork_pid == -1)
            printf("%s: unknown error\n", args[0]);
        else {
            job_new->invs[i]->process_id = fork_pid;
            if (out != STDOUT_FILENO)
                close(out);
            if (in !=  STDIN_FILENO)
                close(in);
            in = pipefd[0];
        }
    }

    if (fork_pid > 0)
        wait_child_processes(job_new);
}

void init_child_process(void){
    signal(SIGINT,SIG_DFL);
    signal(SIGQUIT,SIG_DFL);
    signal(SIGTERM,SIG_DFL);
    signal(SIGTSTP,SIG_DFL);
}

void wait_child_processes(job* job_node){
    int i, status;
    for (i = 0; i < job_node->inv_num; i++){
        if (job_node->invs[i]->is_completed)
            continue;
        waitpid(job_node->invs[i]->process_id, &status, WUNTRACED);
        if (WIFSTOPPED(status)){
            add_job(job_node);
            break;
        }
        else
            job_node->invs[i]->is_completed = 1;
    }
}

int main(void){
    init_shell();
    while(1){
        getinput();
        if (!strlen(input))
            continue;
        create_job();
        parse();
        execute();
    }
    return 0;
}
