/*-------------------------------------------------*/
/*                  sync_info_track                */
/*-------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "sync_info_track.h"

SyncInfo* sync_list_head = NULL;        //pointer to the beginning of the SyncInfo list 

//This function adds a new record in the structure
void add_sync_info(char* source, char* target){     
    SyncInfo* newInfo = malloc(sizeof(SyncInfo));   
    strcpy(newInfo->source, source);
    strcpy(newInfo->target, target);
    strcpy(newInfo->status, "idle");
    strcpy(newInfo->last_sync_time, "\0");
    newInfo->active = 1;
    newInfo->error_count = 0;
    newInfo->next = sync_list_head;
    sync_list_head = newInfo;
}

//This function searches for a record in the list according to the source
SyncInfo* find_sync_info(char* source){
    SyncInfo* curr = sync_list_head;
    while(curr){
        if(!strcmp(curr->source, source))
            return curr;
        curr = curr->next;
    }
    return NULL;
}

WatchEntry *watch_list = NULL;          //pointer to the beginning of the WatchEntry list 

//This function adds a new record in the structure
void add_watch_entry(int wd, const char *source){
    WatchEntry *current = watch_list;
    while(current){     //checks if the source already exists, if not it adds it 
        if(!strcmp(current->source, source))
            return;
        current = current->next;
    }

    WatchEntry *new_entry = malloc(sizeof(WatchEntry));
    if(!new_entry){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    new_entry->wd = wd;
    strncpy(new_entry->source, source, 256);
    new_entry->next = watch_list;
    watch_list = new_entry;
}

//This function searches for the source according to the watch descriptor 
char* lookup_source_from_wd(int wd){
    WatchEntry *current = watch_list;
    while(current){
        if(current->wd == wd)
            return current->source;
        current = current->next;
    }
    return NULL;
}

//This function returns the target according to the source
char* get_target_for_source(char *source){
    SyncInfo *info = find_sync_info(source);
    if(info != NULL) 
        return info->target;
    else 
        return NULL;
}