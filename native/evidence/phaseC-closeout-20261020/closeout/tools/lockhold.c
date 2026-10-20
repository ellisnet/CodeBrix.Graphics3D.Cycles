/* Copyright (c) 2026 Jeremy Ellis and contributors
 * SPDX-License-Identifier: Apache-2.0
 * Phase C close-out device-record lock probe: take flock(LOCK_EX) on PATH, hold it
 * SECONDS, release. Prints unix times. With -n, try LOCK_EX|LOCK_NB once and report. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
int main(int argc,char **argv){
  if(argc<3){fprintf(stderr,"usage: lockhold PATH SECONDS | lockhold -n PATH\n");return 2;}
  if(strcmp(argv[1],"-n")==0){int fd=open(argv[2],O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0){perror("open");return 2;}
    if(flock(fd,LOCK_EX|LOCK_NB)==0){printf("probe-acquired %ld\n",(long)time(0));return 0;}
    printf("probe-busy %ld errno=%d\n",(long)time(0),errno);return 1;}
  int fd=open(argv[1],O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0){perror("open");return 2;}
  if(flock(fd,LOCK_EX)!=0){perror("flock");return 2;}
  printf("held %ld\n",(long)time(0));fflush(stdout);
  sleep(atoi(argv[2]));
  flock(fd,LOCK_UN);printf("released %ld\n",(long)time(0));return 0;}
