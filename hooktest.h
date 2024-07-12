#ifndef HOOKTEST_H
#define HOOKTEST_H
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <dlfcn.h>
#include <sched.h>
#include <sys/time.h>

typedef void (*signal_handler)(int);
 
void sig_handler_2(int signum) {
    char*pid_str = getenv("FUZZ");
    int pid = atoi(pid_str);
    kill(pid,14);
    exit(0);
}

int fork_setitimer(){
  int pid = fork();
  if(pid==0){
    unsigned int timeout = 5;
    struct itimerval it;
    signal(14,sig_handler_2);
    it.it_value.tv_sec = (timeout / 1000);
    it.it_value.tv_usec = (timeout % 1000) * 1000;
    setitimer(ITIMER_REAL, &it, NULL);
  }
  return pid;
}

#define fork() \
        fork_setitimer()

#endif /* HOOKTEST_H */