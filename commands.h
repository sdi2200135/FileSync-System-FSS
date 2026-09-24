#ifndef COMMANDS_H
#define COMMANDS_H

#include "log_worker_utils.h"

void shutdown_mode(char* response, int size, int fd_out);
void add_mode(char* buffer, char* response, char* source, char* target, int size, int fd_out);
void cancel_mode(char* buffer, char* response, char* source, int size, int fd_out); 
void status_mode(char* buffer, char* response, char* source, int size, int fd_out);
void sync_mode(char* buffer, char* response, char* source, int size, int fd_out);

#endif