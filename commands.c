/*-------------------------------------------------*/
/*                  commands                       */
/*-------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "commands.h" 

//Function for the termination process
void shutdown_mode(char* response, int size, int fd_out){
    snprintf(response, size, "Shutting down manager...");
    log_entry(response);
    write(fd_out, response, strlen(response) + 1);
    snprintf(response, size, "Waiting for all active workers to finish.");
    log_entry(response);
    write(fd_out, response, strlen(response) + 1);
    snprintf(response, size, "Processing remaining queued tasks.");
    log_entry(response);
    write(fd_out, response, strlen(response) + 1);
    snprintf(response, size, "Manager shutdown complete.");
    log_entry(response);
    write(fd_out, response, strlen(response) + 1);  //updates the fss_out
}

//Function for the addition of a new synchronization
void add_mode(char* buffer, char* response, char* source, char* target, int size, int fd_out){
    if(sscanf(buffer + 4, "%s %s", source, target) == 2){
        snprintf(response, size, "Added directory: %s -> %s", source, target);
        log_entry(response);
        log_entry_file(response);
        write(fd_out, response, strlen(response) + 1);
        snprintf(response, size, "Monitoring started for %s", source);
        log_entry(response);
        log_entry_file(response);
        newline_in_file();
        write(fd_out, response, strlen(response) + 1);

        worker_on(source, target, "ALL", "FULL");
    }
    else{
        snprintf(response, size, "Invalid add command!");
        write(fd_out, response, strlen(response) + 1);
    }
}

//Function for canceling sync monitoring 
void cancel_mode(char* buffer, char* response, char* source, int size, int fd_out){
    if(sscanf(buffer + 7, "%s", source) == 1){
        snprintf(response, size, "Monitoring stopped for %s", source);
        log_entry(response);
        log_entry_file(response);
        newline_in_file();
    }
    else
        snprintf(response, size, "Invalid cancel command!");
    write(fd_out, response, strlen(response) + 1);
}

//Function for showing synchronization status
void status_mode(char* buffer, char* response, char* source, int size, int fd_out){
    if(sscanf(buffer + 7, "%s", source) == 1){
        SyncInfo *new = find_sync_info(source);     //gets target

        snprintf(response, size, "Status requested for %s\nDirectory: %s\nTarget: %s\nLast Sync: %s\nErrors: %d\nStatus: %s", 
            source, new->source, new->target, new->last_sync_time, new->error_count, new->status);
        log_entry(response);
    }
    else
        snprintf(response, size, "Invalid status command!");
    write(fd_out, response, strlen(response) + 1);
}

//Function for direct synchronization
void sync_mode(char* buffer, char* response, char* source, int size, int fd_out){
    if(sscanf(buffer + 5, "%s", source) == 1){
        SyncInfo *new = find_sync_info(source);
    
        snprintf(response, size, "Syncing directory: %s -> %s", source, new->target);
        log_entry(response);
        log_entry_file(response);
        snprintf(response, size, "Sync completed %s -> %s Errors:%d", source, new->target, new->error_count);
        log_entry(response);
        log_entry_file(response);
        newline_in_file();

        worker_on(source, new->target, "ALL", "FULL"); 
    }
    else
        snprintf(response, size, "Invalid sync command!");
    write(fd_out, response, strlen(response) + 1);
}