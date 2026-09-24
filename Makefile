# Compilation flags
CFLAGS = -Wall -g

# Source files
MANAGER_SRCS = fss_manager.c log_worker_utils.c commands.c sync_info_track.c 
MANAGER_OBJS = $(MANAGER_SRCS:.c=.o)

# Targets
all: worker fss_console fss_manager

worker: worker.c
	gcc $(CFLAGS) -o worker worker.c 

fss_console: fss_console.c
	gcc $(CFLAGS) -o fss_console fss_console.c

fss_manager: $(MANAGER_SRCS)
	gcc $(CFLAGS) -o fss_manager $(MANAGER_SRCS)

fss_script:
	chmod +x fss_script.sh

# Run targets
run_man:
	./fss_manager -l "manager_logfile.txt" -c "config_file.txt" -n 5
	
run_cons:
	./fss_console -l "console_logfile.txt"

# Clean targets
clean:
	rm -f *.o fss_manager fss_console worker  
	rm -f fss_manager fss_console worker manager_logfile.txt console_logfile.txt fss_in fss_out
