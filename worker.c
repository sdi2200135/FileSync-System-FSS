/*-------------------------------------------------*/
/*                     worker                      */
/*-------------------------------------------------*/

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#define BUFFER_SIZE 1024        //buffer size for reading/writing

char error_log[2048];           //array that saves error messages
int copied_files = 0;           //counter of successfully copied files
int skipped_files = 0;          //skipped file counter
char changed_file[1024] = "";   //file name that was modified (in case of DELETE)   

//This function logs an error in error_log
void log_error(char *msg){
    strcat(error_log, "- ");
    strcat(error_log, msg);
    strcat(error_log, "\n");
}

//This function copies a file from source to target
void copy_file(const char *src, const char *dst){
    int fd_in = open(src, O_RDONLY);    //opens the file only for reading
    if(fd_in == -1){
        log_error(strerror(errno));
        skipped_files++;
        return;
    }

    int fd_out = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644); //opens the target file for registration (creation or replacement)
    if(fd_out == -1){
        log_error(strerror(errno));
        close(fd_in);
        skipped_files++;
        return;
    }

    char buffer[BUFFER_SIZE];            //buffer transfers data
    ssize_t bytes_read, bytes_written;
    while((bytes_read = read(fd_in, buffer, sizeof(buffer))) > 0){  //copies the content
        bytes_written = write(fd_out, buffer, bytes_read);
        if(bytes_written != bytes_read){    //if copied bytes are less it prints an error
            log_error("Write error");
            skipped_files++;
            break;
        }
    }

    if(bytes_read == -1)        //if it reads the file, copied_files increase
        log_error("Read error");
    else 
        copied_files++;

    close(fd_in);
    close(fd_out);
}

//This function copies a folder in a recursive way ("ALL", "FULL" case)
void copy_directory(const char *src, const char *dst){
    DIR *dir = opendir(src);            //opens source folder
    if(!dir){
        log_error("opendir failed");
        return;
    }

    mkdir(dst, 0755);                   //creates target folder

    struct dirent *entry;
    while((entry = readdir(dir)) != NULL){      //repeats for each file/folder
        if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char path_src[BUFFER_SIZE], path_dst[BUFFER_SIZE];
        snprintf(path_src, sizeof(path_src), "%s/%s", src, entry->d_name);  //gets the full source path 
        snprintf(path_dst, sizeof(path_dst), "%s/%s", dst, entry->d_name);  //gets the full target path

        struct stat st;
        stat(path_src, &st);    //metadata retrieval

        if(stat(path_src, &st) == -1){
            log_error("stat failed");
            continue;
        }

        if(S_ISDIR(st.st_mode))                     //if it is a folder
            copy_directory(path_src, path_dst);     //recursive copy
        else if(S_ISREG(st.st_mode))                //if it is a file
            copy_file(path_src, path_dst);          //calls the function that copies a file
    }

    closedir(dir);
}

int main(int argc, char *argv[]){
    if(argc != 5){
        fprintf(stderr, "Usage: %s <source> <target> <filename> <operation>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *source = argv[1];
    const char *target = argv[2];
    const char *filename = argv[3];
    const char *operation = argv[4];

    error_log[0] = '\0';        //clears error buffer
    changed_file[0] = '\0';     //clears changed_file 

    if(!strcmp(operation, "FULL"))
        copy_directory(source, target);
    else if(!strcmp(operation, "ADDED") || !strcmp(operation, "MODIFIED")){
        char src_file[1024], dst_file[1024];
        snprintf(src_file, sizeof(src_file), "%s/%s", source, filename);
        snprintf(dst_file, sizeof(dst_file), "%s/%s", target, filename);
        copy_file(src_file, dst_file);
    } 
    else if(!strcmp(operation, "DELETED")){
        char dst_file[1024];
        snprintf(dst_file, sizeof(dst_file), "%s/%s", target, filename);
        if(unlink(dst_file) == -1){     //tries to delete a file
            log_error(strerror(errno));
            skipped_files++;
        }
        strncpy(changed_file, filename, sizeof(changed_file) - 1);  //updates changed file
    } 
    else
        log_error("Unknown operation");

    // === EXEC_REPORT ===
    char exec_report[4096];     //keeps exec report 
    int offset = 0;

    offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "\nEXEC_REPORT_START\n");

    //status
    if(error_log[0] == '\0')
        offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "STATUS: SUCCESS\n");
    else if(copied_files > 0) 
        offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "STATUS: PARTIAL\n");
    else 
        offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "STATUS: ERROR\n");
    
    //details according to the operation
    if(!strcmp(operation, "FULL")) 
        offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "DETAILS: %d files copied, %d skipped\n", copied_files, skipped_files);
    else if(!strcmp(operation, "ADDED") || !strcmp(operation, "MODIFIED") || !strcmp(operation, "DELETED")) 
        offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "DETAILS: File: %s\n", filename);
    
    //adds errors
    if(error_log[0] != '\0')
        offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "ERRORS:\n%s", error_log);

    offset += snprintf(exec_report + offset, sizeof(exec_report) - offset, "EXEC_REPORT_END\n\n");

    //writes directly to STDOUT, which is the pipe
    write(STDOUT_FILENO, exec_report, strlen(exec_report));

    return 0;
}
