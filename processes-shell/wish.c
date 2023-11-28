#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <string.h>
#include <fcntl.h>

#define MAX_ARGS 10
#define MAX_LENGTH_ARGS 20
#define MAX_PATHS 10
#define MAX_COMMANDS 5

char error_message[30] = "An error has occurred\n";

int clean(int *file_arg, char ***arguments_arg, int *original_stderr_arg, int *original_stdout_arg);
int main(int argc, char *argv[]) {
    
    int input_len;
    FILE *fp;
    char **command_strings;
    char *command_string;
    int num_commands=0;
    
    int access_path[MAX_PATHS]; //Represents which paths from full_paths are accessible
    char paths[MAX_PATHS][MAX_LENGTH_ARGS]; //Stores the paths
    char full_paths[MAX_PATHS][MAX_LENGTH_ARGS]; //Stores the full_paths (inlcuding the command) i.e /usr/bin/ls
    int pid[MAX_COMMANDS]; //process id
    
    char ** arguments; //list of arguments
    char *piece; //piece of the sliced input
    
    int redirection_flag=0;
    int num_paths=1; //Number of paths
    int argcount=0; //number of arguments 
    char *filename; //name of the file used for redirection
    
    int original_stderr = dup(STDERR_FILENO);
    int original_stdout= dup(STDOUT_FILENO);
    int file=-1; //file used in redirection mode
    
    
    // set intial access_path
    for(int i=0; i < MAX_PATHS; i++) {
        access_path[0] = 0;
    }
    //set initial path
    strcpy(paths[0], "/bin");
    
    
    if (argc > 2) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        exit(1);
    }
    
    if (argc == 2) {
        // open the file in read mode
        fp = fopen(argv[1], "r");
        if (fp == NULL) {
          write(STDERR_FILENO, error_message, strlen(error_message));
          exit(1);
        }
        
        while(1) {
            char *input=NULL;
            size_t input_size=0;
            command_strings = (char **) malloc(sizeof(char) * MAX_LENGTH_ARGS * MAX_COMMANDS);
            num_commands=0;
            
            //Read a line from the input file
            input_len = getline(&input, &input_size, fp);
            if(input_len == -1) {
                if (!feof(fp)) write(STDERR_FILENO, error_message, strlen(error_message));
                 fclose(fp);
                 free(input);
                 free(command_strings);
                 exit(0);        
            }
                        
            
            while((command_string = strsep(&input, "&")) != NULL) {
              size_t len = strlen(command_string);
              // remove the \n character which has ascii code 10
              if (command_string[len-1] == 10) {
                      command_string[len-1] = '\0';
              }
              
              command_strings[num_commands] = command_string;
              num_commands++;
            }
            
            
            for (int i=0; i < num_commands; i++) {
                //initialize some variables
                argcount = 0;
                redirection_flag = 0;
                int file_num=0;
                
                //restore access paths
                for(int j=0; j< MAX_PATHS; j++) { 
                    access_path[j]=0;
                }
                
                //restore full paths
                for (int j=0; j<num_paths; j++) { 
                    strcpy(full_paths[j], paths[j]);
                }
                
                // Max 10 args of MAX_LENGTH_ARGS bytes
                arguments = (char **) calloc(MAX_ARGS, MAX_LENGTH_ARGS);
                
                int empty=1;
                //slice line into constituent pieces using the space character as delimiter
                while((piece = strsep(&command_strings[i], " ")) != NULL) {
                    size_t piece_len = strlen(piece);
                
                    //remove the \n character which has ascii code 10
                    if (piece[piece_len-1] == 10) piece[piece_len-1] = '\0';
                    
                    
                    if (strlen(piece) == 0) {
                        continue;
                    }
                    
                    //check if there is redirection
                    if (strstr(piece, ">") != NULL) {
                        empty=0;
                        char *s;
                        if (strcmp(piece, ">") == 0) {
                            redirection_flag = 1;
                            continue;
                        }
                        s = strsep(&piece, ">");
                        if (s[strlen(s)-1] == 10) s[strlen(s)-1] = '\0';
                        arguments[argcount] = s;
                        argcount++;
                        filename = piece;
                        redirection_flag = 1;
                        continue;
                    }
                    
                    if (redirection_flag){
                        file_num++;
                        filename = piece;
                        continue;
                    }
                    
                    if (strstr(piece, "&") != NULL) continue;
                    
                    arguments[argcount] = piece;
                    argcount++;
                    empty=0;
                }
                
                
                // if more than 1 file is specified for redirection then it's an error
                if (file_num > 1) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    clean (&file, &arguments, &original_stderr, &original_stdout);
                    continue;
                }
                if (empty) {
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    continue;
                }
                if (arguments[0] == NULL) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    continue;
                }
                
                //check if there is redirection
                if (redirection_flag) {
                    //if there is redirection then the last argument should be the file 
                    file = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 00777);
                    if (file == -1) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean (&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                    // we flush the stderr before just in case
                    fflush(stdout);
                    //Redirect stdout to the file 
                    if(dup2(file, STDOUT_FILENO) == -1) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean (&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                    //we flush the stderr before just in case
                    fflush(stderr);
                    //Redirect stderr to the file
                    if(dup2(file, STDERR_FILENO)==-1){ 
                        write(STDERR_FILENO, error_message, strlen(error_message)); 
                        clean(&file, &arguments, &original_stderr, &original_stdout); 
                        continue;
                    }
                }
                
                //Check if it's the built-in command exit
                if (strcmp(arguments[0], "exit") == 0) {
                    if (argcount > 1) write(STDERR_FILENO, error_message, strlen(error_message));
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    fclose(fp);
                    free(command_strings);
                    exit(0);
                }
                //Check if it's the built-in command cd
                else if (strcmp(arguments[0], "cd")==0){ 
                    if (argcount != 2) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean(&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                    char *cd_path = arguments[argcount-1];
                    if (chdir(cd_path)==-1){
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean(&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                }
                //Check if it's the built-in command path
                else if (strcmp(arguments[0], "path") == 0) {
                    for (int j=0; j < argcount -1; j++) {
                        strcpy(paths[j], arguments[j+1]);
                    }
                    num_paths = argcount-1;
                    
                    for (int j=0; j < num_paths; j++) {
                        strcpy(full_paths[j], paths[j]);
                    }
                }
                else {
                    //add the full_path
                    for (int j=0; j < num_paths; j++) {
                        strcat(full_paths[j], "/");
                        strcat(full_paths[j], arguments[0]);
                    }
                    
                    int error=1;
                    for (int j=0; j < num_paths; j++) {
                        //check that the file exists and it's executable
                        if (access(full_paths[j], X_OK) != -1) {
                            access_path[j] = 1;
                            error = 0;
                        }
                    }
                    
                    if (error) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean(&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                }
                
                // create the process
                if ( (pid[i] = fork() ) == -1){
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    continue;
                }
                
                //pid==0
                if (pid[i] == 0) {
                    fclose(fp);
                    // the child process executes the command
                    for (int j=0; j< num_paths; j++) {
                      if (access_path[j] == 1) {
                        if (execv(full_paths[j], arguments) == -1) {
                          write(STDERR_FILENO, error_message, strlen(error_message));
                        }
                      }
                    }
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    
                    exit(1);
                }
                //if (redirection_flag) close(file);
                clean(&file, &arguments, &original_stderr, &original_stdout);

            
            }
            //waiting for the child process
            for (int j=0; j<num_commands; j++) {
              waitpid(pid[j], NULL, 0);
            }
            free(command_strings);
        }
        
    }
    
    
    
    else {
        while(1) {
            //prompt
            printf("wish> ");
            
            //initialize variables
            char *input=NULL;
            size_t input_size=0;
            command_strings = (char **) malloc(sizeof(char) * MAX_LENGTH_ARGS * MAX_COMMANDS);
            num_commands=0;
            
            //Read a line from the input file
            input_len = getline(&input, &input_size, stdin);
            if(input_len == -1) {
                 free(input);
                 free(command_strings);
                 exit(0);        
            }
            
            
            while((command_string = strsep(&input, "&")) != NULL) {
              size_t len = strlen(command_string);
              // remove the \n character which has ascii code 10
              if (command_string[len-1] == 10) {
                      command_string[len-1] = '\0';
              }
              
              command_strings[num_commands] = command_string;
              num_commands++;
            }
            
            
            
            for (int i=0; i < num_commands; i++) {
                //initialize some variables
                argcount = 0;
                redirection_flag = 0;
                int file_num=0;
                
                //restore access paths
                for(int j=0; j< MAX_PATHS; j++) { 
                    access_path[j]=0;
                }
                
                //restore full paths
                for (int j=0; j<num_paths; j++) { 
                    strcpy(full_paths[j], paths[j]);
                }
                
                // Max 10 args of MAX_LENGTH_ARGS bytes
                arguments = (char **) calloc(MAX_ARGS, MAX_LENGTH_ARGS);
                
                
                //slice line into constituent pieces using the space character as delimiter
                while((piece = strsep(&command_strings[i], " ")) != NULL) {
                    size_t piece_len = strlen(piece);
                
                    //remove the \n character which has ascii code 10
                    if (piece[piece_len-1] == 10) piece[piece_len-1] = '\0';
                    
                    if (strlen(piece) == 0) continue;
                    
                    //check if there is redirection
                    if (strstr(piece, ">") != NULL) { 
                        char *s;
                        if (strcmp(piece, ">") == 0) {
                            redirection_flag = 1;
                            continue;
                        }
                        s = strsep(&piece, ">");
                        if (s[strlen(s)-1] == 10) s[strlen(s)-1] = '\0';
                        arguments[argcount] = s;
                        argcount++;
                        filename = piece;
                        redirection_flag = 1;
                        continue;
                    }
                    
                    if (redirection_flag){
                        file_num++;
                        filename = piece;
                        continue;
                    }
                    
                    if (strstr(piece, "&") != NULL) continue;
                    
                    arguments[argcount] = piece;
                    argcount++;
                }
                
                // if more than 1 file is specified for redirection then it's an error
                if (file_num > 1) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    clean (&file, &arguments, &original_stderr, &original_stdout);
                    continue;
                }
                
                if (argcount==0) {
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    continue;
                }
                
                if (arguments[0] == NULL) {
                    write(STDERR_FILENO, error_message, strlen(error_message)); 
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    continue;
                }
                
                //check if there is redirection
                if (redirection_flag) {
                    //if there is redirection then the last argument should be the file 
                    file = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 00777);
                    if (file == -1) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean (&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                    
                    // we flush the stderr before just in case
                    fflush(stdout);
                    //Redirect stdout to the file 
                    if(dup2(file, STDOUT_FILENO) == -1) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean (&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                    //we flush the stderr before just in case
                    fflush(stderr);
                    //Redirect stderr to the file
                     if(dup2(file, STDERR_FILENO)==-1){ 
                        write(STDERR_FILENO, error_message, strlen(error_message)); 
                        clean(&file, &arguments, &original_stderr, &original_stdout); 
                        continue;
                    }
                }
                
                //Check if it's the built-in command exit
                if (strcmp(arguments[0], "exit") == 0) {
                    if (argcount > 1) write(STDERR_FILENO, error_message, strlen(error_message));
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    free(command_strings);
                    exit(0);
                }
                else if (strcmp(arguments[0], "cd")==0){ 
                    if (argcount != 2) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean(&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                    char *cd_path = arguments[argcount-1];
                    if (chdir(cd_path)==-1){
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean(&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                }
                //Check if it's the built-in command path
                else if (strcmp(arguments[0], "path") == 0) {
                    for (int j=0; j < argcount -1; j++) {
                        strcpy(paths[j], arguments[j+1]);
                    }
                    num_paths = argcount-1;
                    
                    for (int j=0; j < num_paths; j++) {
                        strcpy(full_paths[j], paths[j]);
                    }
                }
                else {
                    //Check if it's the built-in command cd
                    
                    //add the full_path
                    for (int j=0; j < num_paths; j++) {
                        strcat(full_paths[j], "/");
                        strcat(full_paths[j], arguments[0]);
                    }
                    
                    int error=1;
                    for (int j=0; j < num_paths; j++) {
                        //check that the file exists and it's executable
                        if (access(full_paths[j], X_OK) != -1) {
                            access_path[j] = 1;
                            error = 0;
                        }
                    }
                    
                    if (error) {
                        write(STDERR_FILENO, error_message, strlen(error_message));
                        clean(&file, &arguments, &original_stderr, &original_stdout);
                        continue;
                    }
                }
                // create the process
                if ( (pid[i] = fork() ) == -1){
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    continue;
                }
                
                //pid==0
                if (pid[i] == 0) {
                    fclose(stdin);
                    // the child process executes the command
                    for (int j=0; j< num_paths; j++) {
                      if (access_path[j] == 1) {
                        if (execv(full_paths[j], arguments) == -1) {
                          write(STDERR_FILENO, error_message, strlen(error_message));
                        }
                      }
                    }
                    clean(&file, &arguments, &original_stderr, &original_stdout);
                    free(command_strings);
                    exit(1);
                }
                //if (redirection_flag) close(file);
                clean(&file, &arguments, &original_stderr, &original_stdout);

            
            }
            //waiting for the child process
            for (int j=0; j<num_commands; j++) {
              waitpid(pid[j], NULL, 0);
            }
            free(command_strings);
        }
        
        
        
    }
    
    
    return 0;
}

int clean( int *file_arg, char ***arguments_arg, int *original_stderr_arg, int *original_stdout_arg)
{
  if (*file_arg != -1) close(*file_arg);
  if (*arguments_arg != NULL) free(*arguments_arg);
  *arguments_arg = NULL;
  // we flush the stdout and stderr before just in case
  fflush(stdout);
  fflush(stderr);
  dup2(*original_stdout_arg, STDOUT_FILENO);
  dup2(*original_stdout_arg, STDOUT_FILENO);
  fflush(stdout);
  fflush(stderr);
  return 0;
}















