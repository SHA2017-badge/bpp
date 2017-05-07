#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "bppsource.h"

int main(int argc, char **argv) {
	if (argc<3) {
		printf("Usage: %s file.txt subtype\n", argv[0]);
		exit(0);
	}
	FILE *f=fopen(argv[1], "r");
	int subtype=atoi(argv[2]);
	if (f==NULL) {
		perror(argv[1]);
		exit(1);
	}
	int con=bppCreateConnection("localhost", 2);
	if (con<0) exit(1);
	while(1) {
		char buff[1024];
		if (feof(f)) rewind(f);
		fgets(buff, 1024, f);
		buff[strlen(buff)-1]=0; //kill newline
		bppSend(con, subtype, buff, strlen(buff));
		printf("%s\n", buff);
		usleep(100000);
	}
}