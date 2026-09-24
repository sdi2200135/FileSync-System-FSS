/*-------------------------------------------------*/
/*                  fss_manager                    */
/*-------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <errno.h>

#include "commands.h"

#define FIFO_IN "fss_in"
#define FIFO_OUT "fss_out"
#define WORKER_LIMIT 5      //max of workers

int main(int argc, char* argv[]){           
    char path[256] = "";    //defines a variable for the file's path
    int worker_limit; 

    for(int i = 1; i < argc; i++){                          //parsing command line
        if(!strcmp(argv[i], "-c") && i+1 < argc)            //checks if there is a "-c" parameter and copies the path
            strcpy(path, argv[i+1]);
        else if(!strcmp(argv[i], "-n") && i+1 < argc){      //checks if there is a "-n" parameter 
            worker_limit = atoi(argv[i+1]);                 //saves the worker limit in a variable and checks if it is right
            if(worker_limit != WORKER_LIMIT)
                worker_limit = WORKER_LIMIT;
        }
    }

    if(!strlen(path)){      //checks if the path is set
        fprintf(stderr, "Error: No config file!");
        exit(EXIT_FAILURE);
    }

    sigchld_handler(SIGCHLD);               //calls the sigchld_handler function for the child

    mkfifo(FIFO_IN, 0600);                  //creates fss_in FIFO
    mkfifo(FIFO_OUT, 0600);                 //creates fss_out FIFO

    int fd_in = open(FIFO_IN, O_RDONLY);    //opens fss_in for reading only
    int fd_out = open(FIFO_OUT, O_WRONLY);  //opens fss_out for writing only

    if(fd_in == -1 || fd_out == -1){
        perror("Error opening FIFOs");
        exit(EXIT_FAILURE);
    }
    
    FILE *config_file = fopen(path, "r");   //opens config file
    if(!config_file){
        perror("Cannot open config file");
        exit(EXIT_FAILURE);
    }

    char message[256];                  //creates an array for the message form config file
    char response[400];                 //creates an array for the response 

    int inotify_fd = inotify_init();    //defines a variable for file tracking and initializes it 
    if(inotify_fd == -1){
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    while(fgets(message, sizeof(message), config_file)){    //reads the config file line by line
        char source[150], target[150];
        sscanf(message, "(%[^,],%[^)])", source, target);   //analyzes the message into source and target
        worker_on(source, target, "ALL", "FULL");           //synchronizes files in the folder

    
        add_sync_info(source, target);                      //adds information in the structure for information of the source file 

        int wd = inotify_add_watch(inotify_fd, source, IN_CREATE | IN_MODIFY | IN_DELETE);  //adds tracking for creation, modification, or deletion
        if(wd == -1){
            perror("inotify_add_watch");
            continue;
        }
        add_watch_entry(wd, source);        //adds the source in a structure
    }

    fclose(config_file);
    
    while(1){ 
        fd_set readfds;                  //define the set of file descriptors for reading
        FD_ZERO(&readfds);               //initializes the read_fds set
        FD_SET(fd_in, &readfds);         //adds fss_in to read_fds set
        FD_SET(inotify_fd, &readfds);    //adds inotify_fd to read_fds set

        int max_fd;                      //defines a variable for the maximum file descriptor and initializes it
        if(fd_in > inotify_fd)
            max_fd = fd_in;
        else    
            max_fd = inotify_fd;

        if(select(max_fd + 1, &readfds, NULL, NULL, NULL) == -1){   //waiting for data entry  
            if(errno == EINTR) 
                continue;
            perror("select");
            exit(EXIT_FAILURE);
        }

        if(FD_ISSET(fd_in, &readfds)){                      //checks if there is data in fss_in
            char buffer[300];                               //defines a variable to read data
            int n = read(fd_in, buffer, sizeof(buffer));    //reads data  
            if (n <= 0) 
                continue;

            buffer[n] = '\0';

            char source[150], target[150];
            int size = sizeof(response);
            
            if(!strcmp(buffer, "shutdown")){              //if "shutdown" command -> the program stops
                shutdown_mode(response, size, fd_out);
                break;
            }
            else if(!strncmp(buffer, "add ", 4)){         //checks if the command starts with "add"
                add_mode(buffer, response, source, target, size, fd_out);

                int wd = inotify_add_watch(inotify_fd, source, IN_CREATE | IN_MODIFY | IN_DELETE);  //adds the source for tracking
                if(wd == -1){
                    perror("inotify_add_watch");
                    continue;
                }
                add_watch_entry(wd, source);
            }
            else if(!strncmp(buffer, "cancel ", 7)){      //checks if the command starts with "cancel"
                cancel_mode(buffer, response, source, size, fd_out);

                WatchEntry *current = watch_list, *prev = NULL; 
                while(current){                                     //running through the watchlist
                    if(!strcmp(current->source, source)){           //if it finds the source it removes it
                        inotify_rm_watch(inotify_fd, current->wd);
                        if(prev)                                    //updates the watchlist 
                            prev->next = current->next;
                        else 
                            watch_list = current->next;
                        free(current);
                        break;
                    }
                    prev = current;             //updates the previous and the next
                    current = current->next;
                }
            }
            else if(!strncmp(buffer, "status ", 7))       //checks if the command starts with "status"
                status_mode(buffer, response, source, size, fd_out);
            else if(!strncmp(buffer, "sync ", 5))         //checks if the command starts with "sync"
                sync_mode(buffer, response, source, size, fd_out);
            else{                                         //checks if the command is invalid  --> invalid input
                snprintf(response, sizeof(response), "Invalid input: %s", buffer);
                write(fd_out, response, strlen(response) + 1);  //writes the response in fss_out for the user
                log_entry(response);
            }
        }

        if(FD_ISSET(inotify_fd, &readfds)){                     //checks if there are changes monitored files
            char buf[4096];                                     //creates a variable for the inotify data
            ssize_t len = read(inotify_fd, buf, sizeof(buf));   //reads the data that have changed
            
            if(len > 0){        //if there is data
                int i = 0;
                while(i < len){ //it processes the events
                    struct inotify_event *event = (struct inotify_event *)&buf[i];  //gets the event

                    if(event->len){     //if the event exists
                        char *operation = NULL;

                        if(event->mask & IN_CREATE)         //checks if the event is creation
                            operation = "ADDED";
                        else if(event->mask & IN_MODIFY)    //checks if the event is modification
                            operation = "MODIFIED";
                        else if(event->mask & IN_DELETE)    //checks if the event is deletion
                            operation = "DELETED";
                        
                        if(operation){      //if the operation exists
                            char *source_dir = lookup_source_from_wd(event->wd);    //it searches the source from the watch descriptor
                            
                            if(source_dir)  //if the source exists it calls the worker to process the change  
                                worker_on(source_dir, get_target_for_source(source_dir), event->name, operation);
                            else 
                                fprintf(stderr, "Unknown watch descriptor %d\n", event->wd);
                        }
                    }

                    i += sizeof(struct inotify_event) + event->len;     //moves to the next event
                }
            }
        }
    }

    //close
    close(fd_out);
    close(fd_in);
    //deletion of FIFO's
    unlink(FIFO_IN);
    unlink(FIFO_OUT);
    //close inotify descriptor
    close(inotify_fd);
    return 0;
}

