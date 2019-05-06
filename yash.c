#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h> 
#include <fcntl.h>

#define MAX_NUMBER_JOBS 20
#define MAX_LENGTH_COMMAND 2000
#define MAX_LENGTH_TOKEN 30
#define FILE_PERMISSIONS S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH

typedef struct node {
    int id;
    int pid;
    int fg;
    char status[MAX_LENGTH_COMMAND];
    char command[MAX_LENGTH_COMMAND];
    //struct node *next;
} node_t;

node_t jobs[MAX_NUMBER_JOBS];
node_t last_process;

int split_string(char string[], char delim[], char*** result);
void sigint_handler(int signal);
void sigtstp_handler(int signal);
void sigchld_handler(int signal);

void print_list(node_t list[]);
void push(node_t * head, int val);
int pop(node_t ** head);
int remove_last(node_t * head);
int remove_by_index(node_t ** head, int n);

pid_t current_fg = -1;
pid_t child_id = -1;
pid_t last_stopped, last_background;

int job_id = 0;
int job_len = 0;

int main(int argc, char** argv) {
    char command[MAX_LENGTH_COMMAND];
    char **tokens_a, **tokens_b, **tmp_split, **running_command, **tokens[2];
    char *e;
    int token_len, i, j, a, command_len, stop_count, status;
    int pfd[2], len_join[2]; 
    int fd_write, fd_read, fd_error, null_file;
    int background;
    pid_t cpid[2], cpid2, cpid3, cpid_grp;

    // Signal handling
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    
    while(1) {
        // Prints the main prompt (# ) and then waits for user input
        strcpy(command, "");
        printf("# ");
        fgets(command, MAX_LENGTH_COMMAND, stdin);
        fflush(stdin);
        fflush(stdout);

        if(feof(stdin) || strcmp(command, (char *) "exit") != 0) {
            // ^D was input
            printf("\nExiting yash...\n");
            exit(0);
        } else if(strcmp(command, "\n") != 0) {
            // We are not exiting, so we can continue with the application
            if(command[strlen(command)] == '&')
                background = 1;
            // Clear \n
            for(i = 0; i < strlen(command); i++) {
                if(command[i] == '\n')
                    command[i] = '\0';
            }

            signal(SIGTSTP, sigtstp_handler);

            // Create parent process
            cpid_grp = fork();
            if(cpid_grp == 0) {
                
                jobs[job_id].id = job_id+1;
                jobs[job_id].pid = getpid();
                jobs[job_id].fg = 1;
                strcpy(jobs[job_id].status,"Running");
                strcpy(jobs[job_id].command,command);
                job_id++;
                job_len++;

                child_id = getpid();
                // Pipe control: first, check the piping
                if(strstr(command, "|") != NULL) {
                    split_string(command, "|", &tmp_split);
                    len_join[0] = split_string(tmp_split[0], " ", &tokens_a);
                    len_join[1] = split_string(tmp_split[1], " ", &tokens_b);
                    tokens[0] = tokens_a;
                    tokens[1] = tokens_b;
                    pipe(pfd);
                    command_len = 2;
                } else {
                    len_join[0] = split_string(command, " ", &tokens_a);
                    command_len = 1;
                    tokens[0] = tokens_a;
                }

                if(!strcmp(tokens[0][0],"jobs")) {
                    print_list(jobs);
                } else if(!strcmp(tokens[0][0],"fg")){
                    for(j = 0; j < job_len; j++) {
                        if(jobs[j].id == last_background) {
                            strcpy(jobs[j].status, "Running");
                            kill(last_background, SIGCONT);
                            printf("%s", jobs[j].command);
                        }
                    }
                } else if(!strcmp(tokens[0][0],"bg")){
                    for(j = 0; j < job_len; j++) {
                        if(jobs[j].id == last_stopped) {
                            strcpy(jobs[j].status, "Running");
                            kill(last_stopped, SIGCONT);
                            last_background = last_stopped;
                        }
                    }
                } else {
                    for(a = 0; a < command_len; a++) {
                        cpid[a] = fork();
                        if(cpid[a] == 0) {
                            child_id = getpid();
                            if(command_len == 2) {
                                if(a == 0) {
                                    close(pfd[0]);
                                    dup2(pfd[1], STDOUT_FILENO);
                                    setsid();
                                } else if(a == 1) {
                                    close(pfd[1]);
                                    dup2(pfd[0], STDIN_FILENO);
                                    setpgid(0, cpid_grp);
                                }
                            }

                            dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);
                            stop_count = 0;
                            j = 0;
                            // Redirection: next, change redirections
                            for(i = 0 ; i < len_join[a]; i++) {
                                if(!strcmp(tokens[a][i],"<")) {
                                    // Input change
                                    fd_read = open(tokens[a][i+1], O_RDONLY);
                                    if(fd_read != -1)
                                        dup2(fd_read, STDIN_FILENO);
                                    else {
                                        perror("Error");
                                        exit(-1);
                                    }
                                    
                                    stop_count = 1;
                                    //close(fd_read);
                                } else if(!strcmp(tokens[a][i],">")) {
                                    // Output change
                                    fd_write = open(tokens[a][i+1], O_CREAT|O_WRONLY|O_TRUNC, FILE_PERMISSIONS);
                                    dup2(fd_write, STDOUT_FILENO);
                                    stop_count = 1;
                                    //close(fd_write);
                                } else if(!strcmp(tokens[a][i],"2>")) {
                                    // Error Output change
                                    fd_error = open(tokens[a][i+1], O_CREAT|O_WRONLY|O_TRUNC, FILE_PERMISSIONS);
                                    dup2(fd_error, STDERR_FILENO);
                                    stop_count = 1;
                                    //close(fd_error);
                                } else {
                                    if(stop_count == 0)
                                        j++;
                                }
                            }
                            
                            running_command = malloc(sizeof(char *) * j);
                            for(i = 0; i < j; i++) {
                                running_command[i] = malloc(sizeof(char) * MAX_LENGTH_TOKEN);
                                strcpy(running_command[i], tokens[a][i]);
                            } 

                            cpid2 = fork();
                            if(cpid2 == 0) {
                                child_id = getpid();
                                execvp(running_command[0], running_command);
                            } else
                                wait((int *) NULL);

                            //perror("Parent finished");
                            close(fd_read);
                            close(fd_write);
                            close(fd_error);

                            exit(0);
                        } else {
                            // Parent process
                            wait((int *) NULL);   
                        }
                        
                        // For output redirection
                        dup2(STDOUT_FILENO, pfd[1]);
                    }
                }
                //printf("%d %d\n", cpid[0], cpid[1]);
                //print_list(jobs);

                kill(cpid[0], SIGKILL);
                kill(cpid[1], SIGKILL);

                exit(0);
            } else {
                current_fg = cpid_grp;
                if(background == 0)
                    wait((int *) NULL);
            }
            current_fg = -1;
            strcpy(jobs[job_id-1].status,"Done");
        }
    }
    return 0;
}

/** 
 * Name: split_string
 * Description: takes a string and divides it into substrings in the specified string
 * Source: Codingame 
 * URL: https://www.codingame.com/playgrounds/14213/how-to-play-with-strings-in-c/string-split
*/

// TODO make it less repetitive
int split_string(char string[], char delim[], char*** result) {
    // Two copies of the input string
    char str[MAX_LENGTH_COMMAND], str_cpy[MAX_LENGTH_COMMAND];
    strcpy(str,string);
    strcpy(str_cpy,str);
    
    // Other integer variables
    int i, count;
    int init_size = strlen(str);

    // Split functionality with strtok (change characters to '\0')
	char *ptr = strtok(str_cpy, delim);

    // Count the number of substrings
    count = 0;
    while(ptr != NULL) {
        count++;
        ptr = strtok(NULL, delim);
    }

    // Dynamic allocation
    *result = (char**) malloc(count * sizeof(char*));
    
    // Reset split
    ptr = strtok(str, delim);
    
    count = 0;
	while(ptr != NULL) {    
        (*result)[count] = (char *)malloc((MAX_LENGTH_TOKEN+1) * sizeof(char));
        strcpy((*result)[count], ptr);
		ptr = strtok(NULL, delim);
        //printf("%s\n", (*result)[count]);
        count++;
	}

    return count;
}

/**
 * Name: sigint_handler
 * Description: handles the exception caused by the signal SIGINT (^C)
*/
void sigint_handler(int signal) {
    //printf("Caught signal %d\n", signal);
    if(current_fg != -1) {
        kill(current_fg, SIGKILL);
        printf("\n");
    }
}

/**
 * Name: sigtstp_handler
 * Description: handles the exception caused by the signal SIGTSTP (^Z)
*/
void sigtstp_handler(int signal) {
    //printf("Caught signal %d\n", signal);
     if(current_fg != -1) {
        printf("\n");
        kill(current_fg, SIGTSTP);
        kill(current_fg, SIGKILL);
    }
}

/**
 * Name: sigchld_handler
 * Description: handles the exception caused by the signal SIGCHLD
*/
void sigchld_handler(int signal) {
    if(current_fg == -1)
        //kill(program_id, SIGCONT);
        printf("Hola");    
}

/**
 * Name: Linked List library
 * Description: functions for the use of a linked list
 * Source: Learn-c.org
 * URL: https://www.learn-c.org/en/Linked_lists
*/
void print_list(node_t list[]) {
    //node_t current[MAX_NUMBER_JOBS] = head;
    int i;

    for(i = 0; i < job_len; i++) {
        printf("[%d]", list[i].id);
        if(list[i].fg == 1)
            printf("+ ");
        else
            printf("- ");

        printf("%s\t%s\n", list[i].status, list[i].command);
    }
}
