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
void user_input(char command[MAX_LENGTH_COMMAND]);

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
        user_input(command);

        if(feof(stdin)) {
            // ^D was input
            printf("\nExiting yash...\n");
            exit(0);
        } else if(strcmp(command, "\n") != 0) {
            // Clear '\n' from the string 
            for(i = 0; i < strlen(command); i++) {
                if(command[i] == '\n') {
                    command[i] = '\0';
                    break;
                }
            }

            // Checks whether it should be run in background
            if(command[strlen(command)-1] == '&') {
                printf("BACKGROUND\n");
            }
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

void user_input(char command[MAX_LENGTH_COMMAND]) {
    printf("# ");
    fgets(command, MAX_LENGTH_COMMAND, stdin);
    fflush(stdin);
    fflush(stdout);
}