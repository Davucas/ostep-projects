#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_QUEUE_SIZE  20
#define MAX_FILENAMES_SIZES 150

typedef struct producer_args {
    int num_files;
    char **filenames;
} producer_args;

struct buffer {
	char* content;
	int file_index;
	int page_index;
    int page_size;
} queue[MAX_QUEUE_SIZE];
// queue is the circular queue that stores the pages of the files.

struct output_buffers {
	char* letters;
	int* numbers;
	int size;   // both letters and numbers should always have the same size
} *output;
// output is a list of struct output_buffers, one for each compressed page.


/* New approach:
    Use producer-consumer problem approach.
    Divide files into pages instead of doing file_size / getnprocs(), to try to improve the cache performance
    To avoid overflow and segfaults use circular queue (you can try also linked lists)
    
    Implementation:
    Files are divided into pages and each page is stored as a struct buffer in the queue. This is done by 1 producer.
    Then each page is compressed and stored as a struct output_buffers in the output list. This is done by multiple consumers.
    I'm going to use a circular queue to store the compressed buffers of each page. This should solve the overflow problem with test 6.
    Once all the files have been read and the threads have finished, I print the final result. I do it in a serial way.
*/

int queue_head =0;  // Head of the queue
int queue_tail =0;  // Tail of the queue
int queue_size=0;   // Size of the queue
int num_pages=0;    // Total number of pages
int page_size=0;    // Page size of the system
int *pages_per_file;    // List of pages in each file
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER, full = PTHREAD_COND_INITIALIZER;
int done = 0;   // Used together with conditional variables to signal when the producer is done and wake up consumers


// This is like the python method append, which adds an element to the head of the queue
void append(struct buffer buff) {
        queue[queue_head] = buff;
        queue_head = (queue_head + 1) % MAX_QUEUE_SIZE; // Next postition in a circular array is always (head+1)%sizeofarray
        queue_size++;
}

// This is like the python method pop, it removes and returns the last element of the array
struct buffer pop() {
        struct buffer buff = queue[queue_tail];
        queue_tail = (queue_tail + 1) % MAX_QUEUE_SIZE; // Next postition in a circular array is always (head+1)%sizeofarray
        queue_size--;
        return buff;
}

struct output_buffers compress(struct buffer buff) {
    struct output_buffers compressed_buf;
    compressed_buf.numbers = malloc(buff.page_size * sizeof(int));
    char* letters = malloc(buff.page_size);

    int index = 0;
    char prev_letter = buff.content[0];
    int count = 1;
    // Same algorithm as in wzip
    for (int i = 1; i < buff.page_size; i++) {
        char current_letter = buff.content[i];
        if (current_letter == prev_letter) {
            count++;
        }
        else {
            compressed_buf.numbers[index] = count;
            letters[index] = prev_letter;
            count = 1;
            index++;
            prev_letter = current_letter;
        }
    }

    // Handling the last character
    letters[index] = prev_letter;
    compressed_buf.numbers[index] = count;
    index++;

    compressed_buf.size = index;
    compressed_buf.letters = realloc(letters, index);   // Note use of realloc
    //free(letters);
    return compressed_buf;
}

void* producer(void *arg) {
    producer_args *args = (producer_args *)arg;
    int fd; // file
    int file_size;  //size of the file
    struct stat s;  // stats of the file
    char *file; // The contents of the file

    for(int i=0; i < args->num_files; i++) {
        // To divide each file in pages
        int pages_file=0;
        int last_page_size=0;

        // Open the file
        fd = open(args->filenames[i], O_RDONLY);
        if (fd == -1) {
            printf("Unable to open the file\n");
            exit(1);
        }

        // Get the size of the file so we can map it into memory
        if (fstat (fd, &s) == -1) {
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

        // Calculating the number of pages and the size of the last page
        pages_file = file_size/page_size;
        last_page_size = file_size % page_size;
        if (last_page_size != 0) {
            pages_file++;
        }
        num_pages += pages_file;
        pages_per_file[i] = pages_file; // We don't need a lock because after trying different options I decided to only implement 1 thread as producer
        
        // For each page in each file we create a buffer
        for (int j = 0; j < pages_file; j++) {
            pthread_mutex_lock(&lock);
            // Check if the queue is full and wait until it's empty
            while(queue_size == MAX_QUEUE_SIZE) {
                pthread_cond_broadcast(&full);
                pthread_cond_wait(&empty,&lock);
            }
            pthread_mutex_unlock(&lock);
            struct buffer b;
            // For the last page
            if (j == pages_file-1) {
                b.page_size = last_page_size;
            }
            else {
                b.page_size = page_size;
            }
            // Set parameters of the buffer
            b.content = file;
            b.file_index = i;
            b.page_index = j;
            // Next page
            file += page_size;
            
            // Acquire lock to write in the queue
            pthread_mutex_lock(&lock);

            append(b);
            // Release the lock and signal full
            pthread_mutex_unlock(&lock);
            pthread_cond_signal(&full);
        }
    }
    // Signaling the other threads that the producer is done
	done = 1;
	pthread_cond_broadcast(&full);

    //pthread_exit(NULL);
	return NULL;
}

void* consumer() {
    // We use a do-while for optimization, instead of writing the same code twice
	do {
		pthread_mutex_lock(&lock);
        // if queue is empty and producer it's not done, signal empty and wait for full
		while (queue_size == 0 && done == 0) {
			pthread_cond_signal(&empty);
			pthread_cond_wait(&full, &lock);
		}
        // If the queue it's empty and the producer is done, then we have finished
		if (done == 1 && queue_size == 0) {
			pthread_mutex_unlock(&lock);
			return NULL;
		}
        // Consume last element from the queue
		struct buffer buff = pop();
		if (done == 0) {
			pthread_cond_signal(&empty);
		}
		pthread_mutex_unlock(&lock);

        // Get index for buffer
        int index = 0;
        for(int i = 0; i < buff.file_index; i++) {
            index += pages_per_file[i];
        }
        index += buff.page_index;

		output[index] = compress(buff);
        //printf("output: %s\n", output[index].letters);
        //printf("output num: %ls\n", output[index].numbers);
        //printf("output size: %d\n", output[index].size);
	} while(!(done == 1 && queue_size == 0));

	return NULL;
}


void print_output() {
    // Note we don't need locks here since this is done serialy by the main process and not by the threads

	char* output_local = malloc(num_pages * page_size * (sizeof(int) + sizeof(char)));
    // We save the start of the output
	char* output_local_start = output_local;

	for (int i = 0; i < num_pages; i++) {
		// Check it's not empty
		if (output[i].size == 0) continue;

        // Check if it's the last page
		if (i != (num_pages - 1)) {
            // If it's not the last page then we check if the last letter of the page is the same as the first letter of the next page
			if (output[i + 1].size != 0) {
                if (output[i].letters[output[i].size - 1] == output[i + 1].letters[0]) {
			        output[i + 1].numbers[0] += output[i].numbers[output[i].size - 1];
			        output[i].size--;
                }
			}		
		}

		for (int j = 0; j < output[i].size; j++) {
			int count = output[i].numbers[j];
			char letter = output[i].letters[j];
			*((int*)output_local) = count;  // casting to be able to assign it to output_local
			output_local += sizeof(int);
			*((char*)output_local) = letter;
			output_local += sizeof(char);
		}
	}
	fwrite(output_local_start, output_local - output_local_start, 1, stdout);
    free(output_local_start);
}


int main(int argc, char* argv[]) {
    //clock_t start, end;
    //double cpu_time_used;
    //start = clock(); // Registro del tiempo de inicio

	if (argc<2) {
		printf("wzip: file1 [file2 ...]\n");
		exit(1);
	}
    if (sizeof(int) != 4) {
        printf("This program assumes that the size of integers is 4 bytes \n");
        exit(1);
    } 

	int totalFiles = argc - 1; // Total number of files to be compressed
	int num_threads = get_nprocs();
    page_size = getpagesize();
	pages_per_file = malloc(sizeof(int) * totalFiles);
	output = malloc(10000 * sizeof(struct output_buffers));

	pthread_t producer_id,consumers_id[num_threads];

    // Create a struct to pass the arguments to the producer
    producer_args *prod_args = (producer_args *) malloc(sizeof(producer_args));
    prod_args->num_files = argc-1;
    prod_args->filenames = &argv[1];
    
    /*
    prod_args->filenames = (char **)malloc(MAX_FILENAMES_SIZES);
    for (int i = 0; i < argc-1; i++) {
        prod_args->filenames[i] = argv[i + 1];
    }
    */

    // Create producer thread (only 1)
	pthread_create(&producer_id, NULL, producer, prod_args);

    // Create consumer threads
	for (int i = 0; i < num_threads; i++) {
        	pthread_create(&consumers_id[i], NULL, consumer, NULL);
    }

    // Join consumers
    for (int i = 0; i < num_threads; i++) {
        pthread_join(consumers_id[i], NULL);
    }

    // Join producer
   	pthread_join(producer_id,NULL);

    // Now print the output
	print_output();

    /*
    if (queue != NULL) {
        if (queue->content != NULL)
            free(queue->content);
        //free(queue);
        printf("aqui\n");
    }

    if (output != NULL) {
        if (output->letters != NULL)
            free(output->letters);
        if (output->numbers != NULL)
            free(output->numbers);
        //free(output);
        printf("aqui2\n");
    }
    */

    free(pages_per_file);
    free(output);
    pthread_mutex_destroy(&lock);

    //end = clock(); // Registro del tiempo de finalización
    //cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    //printf("El tiempo de ejecución fue: %f segundos\n", cpu_time_used);
	return 0;
}