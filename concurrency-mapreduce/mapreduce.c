#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "mapreduce.h"

#define MAX_SIZE_PARTITION 1000

// Implement MR_Emit
// Implement data strcuture to pass the key-value pairs to the reducers
// Implement MR_Run
// Implement MR_Mapper
// Implement MR_Reduce


typedef struct KeyValue {
    char* key;
    char* value;
    struct KeyValue* next;
} KeyValue;

typedef struct Partition {
    KeyValue **content;
    pthread_mutex_t lock;
    int partition_index;
} Partition;


// Global variables
Mapper mapper;
Reducer reducer;
Partitioner partitioner;
Partition* partition_arr;
int num_partitions;
char **file_list;
int file_index=1;
int num_files;

pthread_mutex_t index_lock;
//pthread_mutex_t partition_lock;


unsigned long MR_DefaultHashPartition(char *key, int num_partitions) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++) != '\0')
        hash = hash * 33 + c;
    return hash % num_partitions;
}



void *MR_Mapper() {
    int file_offset;

    // Lock
    pthread_mutex_lock(&index_lock);
    file_offset = file_index;
    file_index++;
    // Release lock
    pthread_mutex_unlock(&index_lock);

    if(file_offset < num_files) {
        printf("filename: %s\n", file_list[file_offset]);
        mapper(file_list[file_offset]);
    }
    else {
        printf("Error in the MR_Mapper, make sure you didn't call more mappers than files in the input\n");
        //exit(1);
    }

    return NULL;
}



void MR_Emit(char *key, char *value) {
    // Generate Key-Value Pair
    KeyValue *keyvaluepair = malloc(sizeof(KeyValue));
    keyvaluepair->key = key;
    keyvaluepair->value = value;

    // Use the partitioner to get the partition corresponding to the key
    unsigned long partition = partitioner(key, num_partitions);
    Partition *prt = (Partition *) &partition_arr[partition];

    // Using a different lock for each partition we only lock the partition that we are using and not all of them
    pthread_mutex_lock(&prt->lock);
    int ind = prt->partition_index;
    prt->content[ind] = keyvaluepair;
    prt->partition_index++;
    pthread_mutex_unlock(&prt->lock);
}



void MR_Run(int argc, char *argv[], Mapper map, int num_mappers, Reducer reduce, int num_reducers, Partitioner partition){
    // Initialize the global variables
    mapper = map;
    reducer = reduce;
    partitioner = partition;
    num_partitions = num_reducers;
    partition_arr = malloc(sizeof(Partition) * num_partitions);
    file_list = argv;
    num_files = argc-1;
    pthread_mutex_init(&index_lock, NULL);
    
    // Initialize the threads
    pthread_t mapper_thread[num_mappers], reducer_thread[num_reducers];
    
    // Initialize the partitions
    for(int i = 0; i < num_partitions; ++i){
        pthread_mutex_init(&partition_arr[i].lock, NULL);
        partition_arr[i].content = malloc(sizeof(KeyValue) * MAX_SIZE_PARTITION);
    }
    
    // Create mapper threads
    for(int i=0; i < num_mappers; ++i)
        pthread_create(&mapper_thread[i], NULL, MR_Mapper, NULL);
    
    // Join mapper threads
    for(int i = 0; i < num_mappers; ++i)
        pthread_join(mapper_thread[i], NULL);
    

    // Free memory
    free(file_list);
    free(partition_arr);
}