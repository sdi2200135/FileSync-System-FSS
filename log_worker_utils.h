#ifndef LOG_WORKER_UTILS_H
#define LOG_WORKER_UTILS_H

#include <sys/wait.h>

#include "sync_info_track.h"

void log_entry(char *entry);
void log_entry_file(char *entry);
void newline_in_file();
void sigchld_handler(int signo);
void worker_on(char *source, char *target, char* filename, char* operation); 
void log_entry_file1(char *source, char *target, pid_t pid, char *operation, char *result, char *details);

#endif