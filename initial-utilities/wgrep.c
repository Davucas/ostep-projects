#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 2048

int main (int argc, char *argv[])
{
  if (argc < 2) {
    printf("wgrep: searchterm [file...] \n");
    exit(1);
  }
  
  FILE *fp;
  char *buffer=NULL;
  size_t len=0;
  int i;
  
  
  if (argc == 2) {
    buffer = (char *) malloc(sizeof(char) * 100);
    while (fgets(buffer, 100, stdin) != NULL) {
      if (strstr(buffer, argv[1]) != NULL) {
        printf("%s", buffer);
      }
    }
  }
  
  else {
    for (i=2; i < argc; i++) {
      fp = fopen(argv[i], "r");
      if (fp == NULL) {
        printf("wgrep: cannot open file \n");
        exit(1);
      }
      
      while (getline(&buffer, &len, fp) != -1) {
        if (strstr(buffer, argv [1]) != NULL) {
          printf("%s", buffer);
        }
      }
      
      free(buffer);
      fclose(fp);
    }
  }
  
  return 0;
}
