#!/bin/bash

for (( i=1; i<=$#; i++ ))
do
  next=$i           #gets the current argument position
  argc=${!next}     #gets the value of the corresponding argument

  if [ $argc == '-p' ]; then        #checks if the argument is -p
        next=$((i+1))
        path="${!next}"             #saves the next argument as path
    elif [ $argc == '-c' ]; then    #checks if the argument is -c
        next=$((i+1))
        command=${!next}            #saves the next argument as command
        if [ $command == 'listAll' ]; then              #checks if command is listAll 
            if [ ! -f "$path" ]; then
                echo "Can't find the: $path"
                exit 1
            else
                while IFS= read -r line || [ -n "$line" ]; do       #reads every line of the path file 
                    [[ -z "$line" ]] && continue                    #ignores blank lines
                    read -ra raw_fields <<< "$line"                 #splits the line into fields
                    [[ "${raw_fields[2]}" != \[* ]] && continue     #if the 3rd field does not start with [, continue
                    clean_line=${line//[\[\]]/}                     #removes brackets from the line
                    read -ra fields <<< "$clean_line"               #re-splits the line into fields
                    
                    date="${fields[0]}"
                    time="${fields[1]}"
                    source="${fields[2]}"
                    target="${fields[3]}"
                    status="${fields[6]}"
                    
                    echo "$source -> $target [Last Sync: $date $time] [$status]"
                done < "$path"
            fi
        elif [ $command == 'listMonitored' ]; then      #checks if command is listMonitored
            if [ ! -f "$path" ]; then
                echo "Can't find the: $path"
                exit 1
            else
                while IFS= read -r line || [ -n "$line" ]; do       #reads every line of the path file
                    [[ -z "$line" ]] && continue                    #ignores blank lines
                    read -ra raw_fields <<< "$line"                 #splits the line into fields
                    [[ "${raw_fields[2]}" != \[* ]] && continue     #if the 3rd field does not start with [, continue
                    clean_line=${line//[\[\]]/}                     #removes brackets from the line
                    read -ra fields <<< "$clean_line"               #re-splits the line into fields

                    date="${fields[0]}"
                    time="${fields[1]}"
                    source="${fields[2]}"
                    target="${fields[3]}"
                    status="${fields[6]}"

                    if [[ "$status" == "SUCCESS" || "$status" == "PARTIAL" ]]; then #shows only SUCCESS or PARTIAL synchronizations
                        echo "$source -> $target [Last Sync: $date $time]"
                    fi
                done < "$path"
            fi
        elif [ $command == 'listStopped' ]; then        #checks if command is listStopped
            if [ ! -f "$path" ]; then
                echo "Can't find the: $path"
                exit 1
            else
                while IFS= read -r line || [ -n "$line" ]; do       #reads every line of the path file
                    [[ -z "$line" ]] && continue                    #ignores blank lines
                    read -ra raw_fields <<< "$line"                 #splits the line into fields
                    [[ "${raw_fields[2]}" != \[* ]] && continue     #if the 3rd field does not start with [, continue
                    clean_line=${line//[\[\]]/}                     #removes brackets from the line
                    read -ra fields <<< "$clean_line"               #re-splits the line into fields

                    date="${fields[0]}"
                    time="${fields[1]}"
                    source="${fields[2]}"
                    target="${fields[3]}"
                    status="${fields[6]}"

                    if [[ "$status" == "ERROR" ]]; then             #shows only ERROR synchronizations
                        echo "$source -> $target [Last Sync: $date $time]"
                    fi
                done < "$path"
            fi
        elif [ $command == 'purge' ]; then              #checks if command is purge
            if [ -f "$path" ]; then                     #checks if it is a file and removes it
                rm "$path"
                echo "Deleting $path..."
                echo "Purge complete."
            elif [ -d "$path" ]; then                   #checks if it is a directory
                while IFS= read -r line || [ -n "$line" ]; do   #reads every line of the path file
                    clean_line=${line//[\[\]]/}                 #removes brackets from the line
                    read -ra fields <<< "$clean_line"           #re-splits the line into fields

                    source="${fields[2]}"
                    target="${fields[3]}"

                    if [ "$source" == "$path" ]; then           #checks if the path is a source directory  
                        echo "The $path is a source directory."
                        break
                    fi

                    if [ "$target" == "$path" ]; then           #checks if the path is a target directory and removes it   
                        rm -rf "$path"
                        echo "Deleting $path..."
                        echo "Purge complete."
                        break
                    fi
                done < "manager_logfile.txt"        #in this case it cannot keep the path because it deletes it
            else    
                echo "Can't find the: $path"
                exit 1 
            fi
        fi
    fi
done