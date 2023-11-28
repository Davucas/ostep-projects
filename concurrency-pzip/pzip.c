#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sysinfo.h>


typedef struct compress_args
{
    int offset;
    int size;
    int thread_id;
} compress_args;


char *file; // memory region where the file will be mapped
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex for synchronization
pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER; // I will use this to make sure the data is printed in order
int current_thread = 0; // Defines which thread should print



void *compress(void *args){
    compress_args *arguments = args;
    char *f = file + arguments->offset;
    int size = arguments->size;

    char prev_letter = f[0];
    char current_letter;
    int count=1;

    int buffer_offset=0;
    char letters[100];
    int numbers[100];

    // READ FILE
    for (int i=1; i < size; i++) {
        current_letter = f[i];

        if (current_letter == prev_letter) {
            count++;
        }
        else {
            numbers[buffer_offset] = count;
            letters[buffer_offset] = prev_letter;
            buffer_offset ++;
            count = 1;
        }      
        prev_letter = current_letter;
    }
    if (prev_letter != '\0' && prev_letter != '\n') {
        numbers[buffer_offset] = count;
        letters[buffer_offset] = prev_letter;
        buffer_offset ++;
    }

    // Acquire lock before printing compressed data
    pthread_mutex_lock(&mutex);

    // Watiting for it's turn
    while (arguments->thread_id != current_thread) {
        pthread_cond_wait(&cond_var, &mutex);
    }

    // We print the data
    for (int l=0; l < buffer_offset; l++) {
        fwrite(&numbers[l], sizeof(int), 1, stdout);
        fwrite(&letters[l], sizeof(char), 1, stdout);
        fflush(stdout);
    }

    current_thread++;
    pthread_cond_broadcast(&cond_var);

    // Release lock
    pthread_mutex_unlock(&mutex);

    pthread_exit(NULL);
    return NULL;
}




int main (int argc, char *argv[]) 
{
    if (argc < 2) {
        printf("pzip: file1 [file2 ...]\n");
        exit(1);
    }
    if (sizeof(int) != 4) {
        printf("This program assumes that the size of integers is 4 bytes \n");
        exit(1);
    }

    int fd;
    int file_size;  //size of the file
    struct stat s;
    int num_proc;   // number of processors currently available in the system
    int piece_size; // size of each piece of the file
    int remainder; // if file_size%piece_size != 0 then we store the remaining size here
    int offset;
    


    for(int i=1; i < argc; i++) {
    
        fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            printf("Unable to open the file\n");
            exit(1);
        }

        /* Get the size of the file. */
        if (fstat (fd, & s) == -1) {
            printf("error with the file");
            close(fd);
            exit(1);
        }
        file_size = s.st_size;

        file = (char *) mmap(0, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

        close(fd);
        if (file == MAP_FAILED) {
            printf("error mmap");
            exit(1);
        }

        // divide the file into pieces for each thread
        num_proc = get_nprocs();    // get number of processors currently available in the system
        piece_size = file_size / num_proc;
        remainder = file_size % num_proc;
        offset = 0;

        pthread_t threads[num_proc];   //list with all the threads

        for (int t=0; t < num_proc; t++) {
            // Create a struct to pass the parameters to the function called by the thread
            compress_args *args = malloc(sizeof *args);
            args->offset = offset;
            args->thread_id = t;

            if (t == num_proc-1) {
                args->size = piece_size + remainder;
                pthread_create(&threads[t], NULL, compress, args);
            }
            else {
                args->size = piece_size;
                pthread_create(&threads[t], NULL, compress, args);
            }

            offset += piece_size;
        }

        // Join threads
        for (int j = 0; j < num_proc; j++) {
            pthread_join(threads[j], NULL);
        }

        // Unmap and close the file
        if (munmap(file, file_size) == -1) {
            perror("munmap");
            exit(1);
        }
    }

    return 0;
}
