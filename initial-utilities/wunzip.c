#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main (int argc, char *argv[]) 
{
  if (argc != 2) {
    printf("wunzip: file1 [file2 ...] \n");
    exit(1);
  }
  
  FILE *fr;
  int num;
  char letter;
  
  for (int i=1; i < argc; i++) {    
    fr = fopen(argv[i], "r");
    if (fr==NULL) {
      fprintf(stderr, "Unable to open file\n");
      exit(1);
    }
    
    while(fread(&num, sizeof(int), 1, fr)) {
      fread(&letter, 1, 1, fr);
      //fwrite(&letter, 1, num, stdout);
      for (int j=0; j < num; j++) {
        printf("%c", letter);
      }      
    }   
    fclose(fr);
  }
  return 0;
}
