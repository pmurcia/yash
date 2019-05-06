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
#define MAX_LENGTH_TOKEN 30
#define FILE_PERMISSIONS S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH
#define MAX_LENGTH_COMMAND 2000

// Job struct
typedef struct job {
    int id;
    int pid;
    int fg;
    char* status;
    char command[MAX_LENGTH_COMMAND];
} job_t;

// Linked list node
typedef struct node {
    job_t *val;
    struct node *next;
} node_t;

// Jobs variables
node_t jobs[MAX_NUMBER_JOBS];
node_t last_process;

// Util functions
int split_string(char string[], char delim[], char*** result);
void sigint_handler(int signal);
void sigtstp_handler(int signal);
void sigchld_handler(int signal);

// List functions
void print_list(node_t *head);
void push(node_t *head, job_t *val);
node_t pop(node_t **head);
node_t remove_last(node_t *head);
node_t remove_by_index(node_t **head, int n);

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
        // Clears command string
        strcpy(command, "");

        // Prints the main prompt (# ) and then waits for user input
        printf("# ");
        fgets(command, MAX_LENGTH_COMMAND, stdin);
        fflush(stdin);
        fflush(stdout);

        if(feof(stdin)) {
            // ^D was input
            printf("\nExiting yash...\n");
            exit(0);
        } else if(strcmp(command, "\n") != 0) {
            // We are not exiting, so we can continue with the application
            if(command[strlen(command) - 1] == '&') {
                //background = 1;
                printf("BACKGROUND");
            }
            // Clear \n
            for(i = 0; i < strlen(command); i++) {
                if(command[i] == '\n')
                    command[i] = '\0';
            }

            signal(SIGTSTP, sigtstp_handler);
            
            job_t *job_parent = jobs[job_id].val;
            job_parent->id = job_id+1;
            job_parent->pid = getpid();
            job_parent->fg = 1;
            strcpy(job_parent->status,"Running");
            strcpy(job_parent->command,command);
            job_id++;
            job_len++;

            // Create parent process
            cpid_grp = fork();
            if(cpid_grp == 0) {
                job_t *job_child = jobs[job_id].val;
                job_child->id = job_id+1;
                job_child->pid = getpid();
                job_child->fg = 1;
                strcpy(job_child->status,"Running");
                strcpy(job_child->command,command);
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
                        job_t *job_i = jobs[j].val;
                        if(job_i->id == last_background) {
                            strcpy(job_i->status, "Running");
                            kill(last_background, SIGCONT);
                            printf("%s", job_i->command);
                        }
                    }
                } else if(!strcmp(tokens[0][0],"bg")){
                    for(j = 0; j < job_len; j++) {
                        job_t *job_i = jobs[j].val;
                        if(job_i->id == last_stopped) {
                            strcpy(job_i->status, "Running");
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
            strcpy(job_parent->status,"Done");
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
 * Name: print_list
 * Description: takes a list, and prints all the values in it
 * */
void print_list(node_t *head) {
    node_t *current = head;
    job_t *current_job;

    while (current != NULL) {
        // TODO Print data
        current_job = current->val;
        printf("[%d]", current_job->id);
        current_job->fg == 1 ? printf(" + ") : printf(" - ");
        printf("%s\t", current_job->status);
        printf("%s\n", current_job->command);
        current = current->next;
    }
}

/**
 * Name: push
 * Description: adds a new element to the last position of the list
 * */
void push(node_t *head, job_t *val) {
    node_t *current = head;
    while (current->next != NULL)
        current = current->next;

    /* now we can add a new variable */
    current->next = malloc(sizeof(node_t));
    current->next->val = val;
    current->next->next = NULL;
}

/**
 * Name: pop
 * Description: removes the first element from the list, and returns an integer
 * */
node_t pop(node_t **head) {
    node_t *retval = NULL;
    node_t *next_node = NULL;

    if (*head == NULL) {
        return *retval;
    }

    next_node = (*head)->next;
    retval->val = (*head)->val;
    free(*head);
    *head = next_node;

    return *retval;
}

/**
 * Name: remove_last
 * Description: self-explanatory
 * */
node_t remove_last(node_t *head) {
    node_t *retval = NULL;
    /* if there is only one item in the list, remove it */
    if (head->next == NULL) {
        retval->val = head->val;
        free(head);
        return *retval;
    }

    /* get to the second to last node in the list */
    node_t *current = head;
    while (current->next->next != NULL) {
        current = current->next;
    }

    /* now current points to the second to last item of the list, so let's remove current->next */
    retval->val = current->next->val;
    free(current->next);
    current->next = NULL;
    return *retval;
}

/**
 * Name: remove_by_index
 * Description: removes an element from the list, selected by a certain value
 * */
node_t remove_by_index(node_t **head, int n) {
    int i = 0;
    node_t *retval = NULL;
    node_t *current = *head;
    node_t *temp_node = NULL;

    if (n == 0) {    
        return pop(head);
    }

    for (i = 0; i < n-1; i++) {
        if (current->next == NULL) {
            return *retval;
        }
        current = current->next;
    }

    temp_node = current->next;
    retval->val = temp_node->val;
    current->next = temp_node->next;
    free(temp_node);

    return *retval;
}