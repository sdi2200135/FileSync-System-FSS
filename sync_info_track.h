#ifndef SYNC_INFO_TRACK_H
#define SYNC_INFO_TRACK_H

#include <time.h>

//structure for monitoring synchronized directories
typedef struct SyncInfo{        //linked list 
    char source[256];           //source path
    char target[256];           //target path 
    char status[20];            //synchronization status
    char last_sync_time[20];    //last synchronization time
    int active;                 //to check if it is ative
    int error_count;            //to keep the counted errors
    struct SyncInfo* next;      //pointer to the next list node
}SyncInfo;

void add_sync_info(char* source, char* target);
SyncInfo* find_sync_info(char* source);

//keeps information about a file that we monitor with inotify 
typedef struct WatchEntry{      //linked list
    int wd;                     //watch descriptor from inotify
    char source[256];           //source path
    struct WatchEntry *next;    //pointer to the next list node
}WatchEntry;

extern WatchEntry *watch_list;

void add_watch_entry(int wd, const char *source);
char* lookup_source_from_wd(int wd);
char* get_target_for_source(char *source);

#endif
