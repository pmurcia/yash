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
    void *val;
    struct node *next;
} node_t;

// Jobs functions
job_t create_job(int id, int pid, int fg, char* status, char command[MAX_LENGTH_COMMAND]);
void print_job(job_t *job);
int* get_pipe_args(char *command, int **pfd, char ****tokens);

// Util functions
int split_string(char string[], char delim[], char*** result);
void sigint_handler(int signal);
void sigtstp_handler(int signal);
void sigchld_handler(int signal);
void user_input(char *command);
int get_new_id();
void program_exit();

// List functions
int init_linked_list(node_t **head);
void print_list(node_t *head);
void push(node_t *head, job_t *val);
node_t pop(node_t **head);
node_t remove_last(node_t *head);
node_t remove_by_index(node_t **head, int n);
int list_length(node_t *head);

pid_t current_fg = -1;
pid_t child_id = -1;

job_t *current, *last_stopped;

node_t *jobs;

int job_id = 1;

int main(int argc, char** argv) {
    char command[MAX_LENGTH_COMMAND];
    char **tokens_a, **tokens_b, **tmp_split, **running_command, ***tokens, **parts;
    char *e, *tmp_com;
    int token_len, i, j, a, pipe_len, stop_count, status, job_len;
    int *pfd, *lengths; 
    int fd_write, fd_read, fd_error, null_file;
    int background;
    pid_t cpid[2], cpid2, cpid3, cpid_grp;

    // Job variables
    //job_t *parent_job, *job;

    // Signal handling
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    // Initialize linked list
    init_linked_list(&jobs);
    
    // Infinite loop in charge of the program
    while(1) {
        // Clears command string
        strcpy(command, "");

        // Prints the main prompt (# ) and then waits for user input
        user_input(command);

        if(feof(stdin)) {
            // ^D was input
            printf("\n");
            program_exit();
        } else if(strcmp(command, "\n") != 0) {
            // Clear '\n' from the string 
            for(i = 0; i < strlen(command); i++) {
                if(command[i] == '\n') {
                    command[i] = '\0';
                    break;
                }
            }

            // Check if 'exit' was introduced
            strcpy(tmp_com, command);
            if(strcmp(strtok_r(tmp_com," ",&tmp_com), "exit") == 0)
                program_exit();

            // Create process for the command executed
            cpid_grp = fork();
            if(cpid_grp == 0) {
                // Process executing the command

                // Checks whether it should be run in background
                background = command[strlen(command)-1] == '&';

                // Create parent job, and push it to the list
                job_t *job = NULL;
                job = (job_t*) malloc(sizeof(job_t));
                *job = create_job(get_new_id(), getpid(), !background, "Running", command);

                current = job;
                
                // Get arguments for pipe and token separation
                lengths = get_pipe_args(command, &pfd, &tokens);

                // Special commands
                if(!strcmp(tokens[0][0],"jobs")) {
                    print_list(jobs);
                } else if(!strcmp(tokens[0][0],"fg")){
                    
                } else if(!strcmp(tokens[0][0],"bg")){
                    
                } else {
                    // Regular command
                    push(jobs,job);
                    
                    // Execute command
                    for(a = 0; a < 2 && lengths[a] != -1; a++) {
                        cpid[a] = fork();
                        if(cpid[a] == 0) {
                            child_id = getpid();
                            if(lengths[a] == 2) {
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
                            for(i = 0 ; i < lengths[a]; i++) {
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
                //exit(0);
            } else {
                // Process running yash
                wait((int*) NULL);
            }
        }
    }
    return 0;
}

/** 
 * Name: split_string
 * Description: takes a string and divides it into substrings in the specified string, returning the number of tokens
 * Source: Codingame 
 * URL: https://www.codingame.com/playgrounds/14213/how-to-play-with-strings-in-c/string-split
*/
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
    if(current != NULL) {
        printf("\n");
        kill(current->pid, SIGKILL);
        current = NULL;
    }
}

/**
 * Name: sigtstp_handler
 * Description: handles the exception caused by the signal SIGTSTP (^Z)
*/
void sigtstp_handler(int signal) {
    //printf("Caught signal SIGTSTP %d\n", signal);
     if(current != NULL) {
        printf("\n");
        kill(current->pid, SIGTSTP);
        last_stopped = current;
        current = NULL;
        kill(current->pid, SIGKILL);
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
 * Name: print_job
 * Description: prints a job
 * */
void print_job(job_t *job) {
    printf("[%d]", job->id);
    job->fg == 1 ? printf(" + ") : printf(" - ");
    printf("%s\t", job->status);
    printf("%s\n", job->command);
}

/**
 * Name: print_list
 * Description: takes a list, and prints all the values in it
 * */
void print_list(node_t *head) {
    node_t *current = head->next;       // First element is NULL, because data starts in the second entry
    job_t *current_job;

    current_job = (job_t *)malloc(sizeof(job_t));

    while (current != NULL) {
        current_job = current->val;
        print_job(current->val);
        current = current->next;
    }
}

/**
 * Name: push
 * Description: adds a new element to the last position of the list
 * */
void push(node_t *head, job_t *val) {
    node_t *current = head;
    while (current->next != NULL) {
        current = current->next;
    }

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

void user_input(char *command) {
    int count = 0;
    char c;

    printf("# ");
    fgets(command, MAX_LENGTH_COMMAND, stdin);
    /*do {
        c = getchar();
        command[count] = c;
        count++;
    } while(c != '\n');
*/
    fflush(stdin);
    fflush(stdout);
}

job_t create_job(int id, int pid, int fg, char* status, char command[MAX_LENGTH_COMMAND]) {
    job_t job;

    job.id = id;
    job.pid = pid;
    job.fg = fg;
    job.status = status;
    strcpy(job.command,command);

    return job;
}

int get_new_id() {
    int id = job_id;
    job_id++;
    return id;
}

void program_exit() {
    printf("Exiting yash...\n");
    exit(0);
}

int init_linked_list(node_t **head) {
    *head = malloc(sizeof(node_t));
    if (*head == NULL) {
        return -1;
    }

    (*head)->next = NULL;
}

int* get_pipe_args(char *command, int **pfd, char ****tokens) {
    char **tmp_split, **tmp, *tmp_com;
    int *lengths;
    int i;

    tmp_com = command;
    *tokens = (char ***) malloc(2*sizeof(char **));
    *pfd = (int *) malloc(2*sizeof(int));
    lengths = (int *) malloc(2*sizeof(int));

    if(strstr(tmp_com, "|") != NULL) {
        split_string(tmp_com, "|", &tmp_split);
        for(i = 0; i < 2; i++) {
            int len = split_string(tmp_split[i], " ", &tmp);
            (*tokens)[i] = (char **) malloc(len*sizeof(char *));
            (*tokens)[i] = tmp;
            lengths[i] = len;
        }
        pipe(*pfd);
    } else {
        int len = split_string(tmp_com, " ", &tmp);
        (*tokens)[0] = (char **) malloc(len*sizeof(char *));
        (*tokens)[0] = tmp;
        (*tokens)[1] = NULL;
        lengths[0] = len;
        lengths[1] = -1;
    }
    
    return lengths;
}

int list_length(node_t *head) {
    node_t *current = head;
    int len = 0;

    while (current->next != NULL)
        len++;

    return len;
}