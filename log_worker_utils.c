/*-------------------------------------------------*/
/*              log_worker_utils                   */
/*-------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "log_worker_utils.h"

#define log_file "manager_logfile.txt"

//This function prints a message to the console along with a timestamp
void log_entry(char *entry) {
    time_t now = time(NULL);                //timestamp now
    struct tm *tm_info = localtime(&now);   //convert to local date and time
    
    char timestamp[20];                                                     //keeps it in form "YYYY-MM-DD HH:MM:SS"
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);   //Formatting the date and time
    printf("[%s] %s\n", timestamp, entry); 
}

//This function prints a message to the log file along with a timestamp
void log_entry_file(char *entry){
    FILE *log = fopen(log_file, "a");   //open the log file
    if(!log) 
        return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char timestamp[20];  
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log, "[%s] %s\n", timestamp, entry);
    
    fclose(log);
}

//This function prints newline after each call of log_entry_file and log_entry_file1
void newline_in_file(){
    FILE *log = fopen(log_file, "a");   
    if(!log) 
        return;

    fprintf(log, "\n");
    fclose(log);
}

//This function is a handler for the SIGCHLD signal and avoids zombie processes
void sigchld_handler(int signo){
    int status;         //status of the child
    pid_t pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0){   //waiting without blocking for a child that has finished
        continue;
    }
}

//This function starts a worker and processes its result
void worker_on(char* source, char* target, char* filename, char* operation){
    pid_t pid;
    int pipefd[2];      //pointers for the pipe(0:reading, 1:writing)

    if(pipe(pipefd) == -1){             //crates a pipe for communication
        perror("Error creating pipe");
        exit(EXIT_FAILURE);
    }

    pid = fork();       //creates a new process
    if(pid == -1){
        perror("Error forking worker");
        exit(EXIT_FAILURE);
    }

    if(pid == 0){           //if child (worker)
        close(pipefd[0]);   //close read end of pipe

        dup2(pipefd[1], STDOUT_FILENO); //redirects stdout to pipe
        close(pipefd[1]);               //close fd after copying

        execl("./worker", "worker", source, target, filename, operation, (char*)NULL);  //execution of worker program
        perror("Error starting worker");
        exit(EXIT_FAILURE);
    } 
    else{       //if parent (manager)
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        
        char timestamp[20];  
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

        close(pipefd[1]);           //close write end of pipe

        char buffer[4096] = {0};    //buffer to read data form pipe
        ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);    //reades form the pipe
        if(n == -1){
            perror("Error reading from pipe");
            close(pipefd[0]);
            return;
        }

        close(pipefd[0]);   //close fd after reading it

        char status[128] = "UNKNOWN";       //status initialization
        char details[512] = "No details";   //details initialization

        char *start_status = strstr(buffer, "STATUS: ");    //locates the STATUS field
        if(start_status){
            start_status += strlen("STATUS: ");             //moves to the data 
            sscanf(start_status, "%s", status);             //reades data's value
        }

        char *start_details = strstr(buffer, "DETAILS: ");          //locates the DETAILS field
        if(start_details){
            start_details += strlen("DETAILS: ");                   //moves to the data 
            strncpy(details, start_details, sizeof(details) - 1);   //copies data
            details[sizeof(details) - 1] = '\0';                    //ends the string
            char *newline = strchr(details, '\n');                  //finds the newline and erases it
            if(newline)
                *newline = '\0';
        }

        log_entry_file1(source, target, pid, operation, status, details);
        newline_in_file();
    
        SyncInfo *new = find_sync_info(source);     //finds the source in the structure 
        if(new == NULL){
            close(pipefd[0]);
            return;
        }
        
        char *start_errors = strstr(buffer, "ERRORS: ");    //locates the ERRORS field
        if(start_errors){
            start_errors += strlen("ERRORS: ");             //moves to the data
            sscanf(start_errors, "%d", &new->error_count);  //reades data's value
        }

        strcpy(new->last_sync_time, timestamp);             //copies the time of the last synchronization
    }
}

//This function prints all elements of an operation in the log file
void log_entry_file1(char *source, char *target, pid_t pid, char *operation, char *result, char *details) {
    FILE *log = fopen(log_file, "a");       //open the log file
    if(!log){
        perror("Failed to open log file");
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char timestamp[20];  
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log, "[%s] [%s] [%s] [%d] [%s] [%s] [%s]\n",
            timestamp, source, target, pid, operation, result, details);
    
    fclose(log);
}