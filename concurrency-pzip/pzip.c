#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <time.h>

#define MAX_SIZE_BUFFER 1000

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

char letters[MAX_SIZE_BUFFER];
int numbers[MAX_SIZE_BUFFER];
int buffer_offset=0;


void *compress(void *args){
    compress_args *arguments = args;
    char *f = file + arguments->offset;
    int size = arguments->size;

    // Making sure it's not empty
    if (size == 0) {
        printf("Size can't be 0\n");
        exit(1);
    }

    char prev_letter = f[0];
    char current_letter='\0';
    int count=1;    // count of times a letter appears in the file
    
    char letters_local[MAX_SIZE_BUFFER];
    int numbers_local[MAX_SIZE_BUFFER];
    int buffer_offset_local=0;
    
    // READ FILE
    for (int i=1; i < size; i++) {
        current_letter = f[i];

        if (current_letter == prev_letter) {
            count++;
        }
        else {

            numbers_local[buffer_offset_local] = count;
            letters_local[buffer_offset_local] = prev_letter;
            buffer_offset_local ++;
            count = 1;
        }
        prev_letter = current_letter;
    }
    // make sure to include the last letter
    if (current_letter != '\0' && current_letter != '\n') {
        numbers_local[buffer_offset_local] = count;
        letters_local[buffer_offset_local] = current_letter;
        buffer_offset_local ++;
    }

    // Acquire lock before printing compressed data
    pthread_mutex_lock(&mutex);

    // Watiting for it's turn
    while (arguments->thread_id != current_thread) {
        pthread_cond_wait(&cond_var, &mutex);
    }

    // if the last letter of the buffer is the same as the first letter of the local buffer we join them
    if (buffer_offset > 0 && letters[buffer_offset-1] == letters_local[0]) {
        buffer_offset_local--;
        numbers[buffer_offset-1] += numbers_local[0];
        memcpy(&numbers[buffer_offset], &numbers_local[1], (buffer_offset_local) * sizeof(int));
        memcpy(&letters[buffer_offset], &letters_local[1], (buffer_offset_local) * sizeof(char));
    }
    else {
        memcpy(&numbers[buffer_offset], numbers_local, (buffer_offset_local) * sizeof(int));
        memcpy(&letters[buffer_offset], letters_local, (buffer_offset_local) * sizeof(char));
    }

    current_thread++;
    buffer_offset += buffer_offset_local;

    pthread_cond_broadcast(&cond_var);
    // Release lock
    pthread_mutex_unlock(&mutex);

    //pthread_exit(NULL);
    return NULL;
}





int main (int argc, char *argv[]) 
{   
    clock_t start, end;
    double cpu_time_used;
    start = clock();




    if (argc < 2) {
        printf("pzip: file1 [file2 ...]\n");
        exit(1);
    }
    if (sizeof(int) != 4) {
        printf("This program assumes that the size of integers is 4 bytes \n");
        exit(1);
    }

    int fd; // file
    int file_size;  //size of the file
    struct stat s;
    int num_proc;   // number of processors currently available in the system
    int piece_size; // size of each piece of the file
    int remainder; // if file_size%piece_size != 0 then we store the remaining size here
    int offset; //offset of each thread inside the file
    
    for(int i=1; i < argc; i++) {
        // Open the file
        fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            printf("Unable to open the file\n");
            exit(1);
        }

        // Get the size of the file so we can map it into memory
        if (fstat (fd, & s) == -1) {
            printf("Error with the file");
            close(fd);
            exit(1);
        }
        file_size = s.st_size;

        // Map de file content in memory
        file = (char *) mmap(0, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (file == MAP_FAILED) {
            printf("Error mmap");
            exit(1);
        }

        // We divide the file into pieces for each thread
        num_proc = get_nprocs();    // get number of processors currently available in the system
        piece_size = file_size / num_proc;  // size if each piece
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
            }
            else {
                args->size = piece_size;
            }
            // Create the threads passing the structure with the arguments
            pthread_create(&threads[t], NULL, compress, args);
            offset += piece_size;
        }

        // Join threads
        for (int j = 0; j < num_proc; j++) {
            pthread_join(threads[j], NULL);
        }

        // Write the compressed output to the standard output
        for (int l=0; l < buffer_offset; l++) {
            fwrite(&numbers[l], sizeof(int), 1, stdout);
            fwrite(&letters[l], sizeof(char), 1, stdout);
            fflush(stdout);
        }

        // Unmap the file content from the memory
        if (munmap(file, file_size) == -1) {
            perror("munmap");
            exit(1);
        }
    }





    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("CPU time used: %f seconds\n", cpu_time_used);
    
    return 0;
}