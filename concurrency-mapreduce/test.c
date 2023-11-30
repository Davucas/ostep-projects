#include "mapreduce.c"



void myMap(char *filename) {
    for (int i=0; i<3; i++) {
        MR_Emit(i, 1);
    }
}

void myReduce() {
    
}


int main(int argc, char *argv[]) {
    MR_Run(argc, argv, myMap, 5, myReduce, 5, MR_DefaultHashPartition);

}