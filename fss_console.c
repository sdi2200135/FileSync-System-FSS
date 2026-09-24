/*-------------------------------------------------*/
/*                  fss_console                    */
/*-------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>

#define FIFO_IN "fss_in"                    //to the manager
#define FIFO_OUT "fss_out"                  //from the manager
#define log_file "console_logfile.txt"

//This function prints a message to the console along with a timestamp
void log_entry(char *entry){
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

    //prints the message in different way according to its form
    if (!strcmp(entry, "shutdown") || !strncmp(entry, "add ", 4) || !strncmp(entry, "cancel ", 7) || !strncmp(entry, "status ", 7) || !strncmp(entry, "sync ", 5))
        fprintf(log, "[%s] Command %s\n", timestamp, entry);
    else
        fprintf(log, "[%s] %s\n", timestamp, entry);
    fclose(log);
}

//This function prints newline after each call of log_entry_file
void newline_in_file(){
    FILE *log = fopen(log_file, "a");   
    if(!log) 
        return;

    fprintf(log, "\n");

    fclose(log);
}

int main(int argc, char* argv[]){
    if(argc != 3)                   
        exit(EXIT_FAILURE);

    if(strcmp(argv[1], "-l")){      //checks the first argument
        fprintf(stderr, "Usage: %s -l <console-logfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int fd_in = open(FIFO_IN, O_WRONLY);    //opens fss_in for writing
    int fd_out = open(FIFO_OUT, O_RDWR);    //opens fss_out for reading and writing

    if(fd_in == -1){    
        perror("Error opening fss_in");
        exit(EXIT_FAILURE);
    }

    if(fd_out == -1){
        perror("Error opening fss_out");
        exit(EXIT_FAILURE);
    }

    char message[300];      //array that stores user's message
    char response[300];     //array that stores manager's response

    while(1){
        printf("> ");                               //prompt for user input 
        fgets(message, sizeof(message), stdin);

        message[strcspn(message, "\n")] = 0;        //removes newline

        if(!strlen(message))    //checks if user typed a message
            continue;

        log_entry_file(message);
        newline_in_file();
        write(fd_in, message, strlen(message) + 1);  //writes the message in fss_in 
        
        //waits for the manager to respond
        fd_set read_fds;            //define the set of file descriptors for reading
        struct timeval timeout;     //define the structure for timeout
        int retval;                 //define the variable for "select()" fuction's result
    
        do{
            FD_ZERO(&read_fds);         //initializes the read_fds set
            FD_SET(fd_out, &read_fds);  //adds fss_out to read_fds set
            timeout.tv_sec = 1;         //sets a timeout of 1 sec
    
            retval = select(fd_out + 1, &read_fds, NULL, NULL, &timeout);  //waits until there is data or the timeout expires
    
            if(retval == -1){           //checks if there is an error
                perror("select()");
                break;
            }
            else if(retval){    //if there is data for reading
                ssize_t bytes_read = read(fd_out, response, sizeof(response) - 1);  //reads data form fss_out
                if(bytes_read > 0){
                    response[bytes_read] = '\0';   //to "end" the string in the correct way
                    log_entry(response);
                    log_entry_file(response);
                }
            }
            //if retval == 0 then timeout ended and there are no more data to read
        }while(retval != 0);
        newline_in_file();

        if(!strcmp(message, "shutdown"))    //if the message from user is "shutdown" then the program stops 
            break;
    }

    //close
    close(fd_out);
    close(fd_in);
    return 0;
}

