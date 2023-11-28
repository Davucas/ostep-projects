#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main (int argc, char *argv[]) 
{
  if (argc < 2) {
    printf("wzip: file1 [file2 ...]\n");
    exit(1);
  }
  if (sizeof(int) != 4) {
    printf("This program assumes that the size of integers is 4 bytes \n");
    exit(1);
  }
  
  FILE *fp;
  char prev_letter;
  char current_letter;
  int count=1;
  
  for(int i=1; i < argc; i++) {
    
    fp = fopen(argv[i], "r");
    if (fp == NULL) {
      printf("Unable to open the file\n");
      exit(1);
    }
    
    fread(&prev_letter, 1, 1, fp);
    
    while (fread(&current_letter, 1, 1, fp)) {
      if (current_letter == prev_letter) {
        count++;
      }
      else {
        // depending on the OS and the configuration it may look like the count is not being written to the stdout, but it is.
        fwrite(&count, sizeof(int), 1, stdout);
        fwrite(&prev_letter, 1, 1, stdout);
        count = 1;
        fflush(stdout);
      }      
      prev_letter = current_letter;
    }
    fclose(fp);    
    
  }
  
  return 0;
}
