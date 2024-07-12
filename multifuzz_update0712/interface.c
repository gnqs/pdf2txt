#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/types.h> 
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/wait.h>

#define FILE_READ_CHUNK 1024
#define SHM_SIZE 65536
#define SOCKET_READ_CHUNK 1024 // SHM_SIZE should be divisible by this

#define SHM_ENV_VAR "__AFL_SHM_ID"

#define LOGFILE "/tmp/afl-wrapper.log"

#define VERBOSE 

#define STATUS_SUCCESS 0
#define STATUS_TIMEOUT 1
#define STATUS_CRASH 2
#define STATUS_QUEUE_FULL 3
#define STATUS_COMM_ERROR 4
#define STATUS_DONE 5

#define MAX_TRIES 40

#define DEFAULT_SERVER "127.0.0.1"
#define DEFAULT_PORT "7007"
#define DEFAULT_NUM 1

#define DEFAULT_MODE 0
#define LOCAL_MODE 1

uint8_t* trace_bits;
int prev_location = 0;

char* ports[20]={"9003","9001","9002","9003","9004",
                  "9005","9006","9007","9008","9009",
                  "9010","9011","9012","9013","9014",
                  "9015","9016","9017","9018","9019"};
char *servers[20] = {"127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1",
                    "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1",
                    "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1",
                    "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1", "127.0.0.1",
                    };
/* Stdout is piped to null when running inside AFL, so we have an option to write output to a file */
FILE* logfile;

/* Running inside AFL or standalone */
uint8_t in_afl = 0;
int crash = 0;

#define OUTPUT_STDOUT
// #define OUTPUT_FILE

void LOG(const char* format, ...) {
  va_list args;
#ifdef OUTPUT_STDOUT
    va_start(args, format);
    vprintf(format, args);
#endif
#ifdef OUTPUT_FILE
    va_start(args, format);
    vfprintf(logfile, format, args);
#endif
  va_end(args);
}

#define DIE(...) { LOG(__VA_ARGS__); if(!in_afl) shmdt(trace_bits); if(logfile != NULL) fclose(logfile); exit(1); }
#define LOG_AND_CLOSE(...) { LOG(__VA_ARGS__); if(logfile != NULL) fclose(logfile); }

#ifdef VERBOSE
  #define LOGIFVERBOSE(...) LOG(__VA_ARGS__);
#else
  #define LOGIFVERBOSE(...) 
#endif

int tcp_socket;
int tcp_sockets[20];

// 尝试从文件描述符fd中读取count个字节到buf中。
// 返回实际读取的字节总数，如果发生错误则返回-1。
ssize_t readn(int fd, void *vptr, size_t n){
    size_t nleft;
    ssize_t nread;
    char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ( (nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                nread = 0;     
            else
                return(-1);
        } else if (nread == 0)
            break;

        nleft -= nread;
        ptr   += nread;
    }
    return(n - nleft);
}

// 尝试将count个字节从buf写入文件描述符fd中。
// 返回实际写入的字节总数，如果发生错误则返回-1。
ssize_t writen(int fd, const void *vptr, size_t n){
    size_t nleft;
    ssize_t nwritten;
    const char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR)
                nwritten = 0;
            else
                return(-1); 
        }

        nleft -= nwritten;
        ptr   += nwritten;
    }
    return(n);
}

/* Set up the TCP connection */
void setup_tcp_connection(const char* hostname, const char* port) {
  LOG("Trying to connect to server %s at port %s...\n", hostname, port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;
  struct addrinfo* res = 0;
  int err = getaddrinfo(hostname, port, &hints, &res);
  if (err!=0) {
    DIE("failed to resolve remote socket address (err=%d)\n", err);
  }

  tcp_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (tcp_socket == -1) {
    DIE("%s\n", strerror(errno));
  }

  if (connect(tcp_socket, res->ai_addr, res->ai_addrlen) == -1) {
    DIE("%s\n", strerror(errno));
  }

  freeaddrinfo(res);
}

void wait_others(int no, const char* hostname, const char* port, int num, uint8_t the_status,uint8_t* father_status) {
  LOG("Waiting for other modules at port %s...\n", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;
  struct addrinfo* res = 0;
  int err = getaddrinfo(hostname, port, &hints, &res);
  if (err!=0) {
    DIE("failed to resolve remote socket address (err=%d)\n", err);
  }

  tcp_sockets[no] = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (tcp_sockets[no] == -1) {
    DIE("%s\n", strerror(errno));
  }

  if (connect(tcp_sockets[no], res->ai_addr, res->ai_addrlen) == -1) {
    DIE("%s\n", strerror(errno));
  }
  int nread2 = 0;
  uint8_t other_status = 0;
  while(num>0){
    nread2 = read(tcp_sockets[no], &other_status, 1);
    if (nread2 == 1) {
      num-=1;
      printf("Connection established %d!\n",num);
    }
    if (other_status != 0 && other_status != 1)
      *father_status = STATUS_CRASH;
    printf("father %d\n",*father_status);
  }
  freeaddrinfo(res);
}

void printUsageAndDie() {
  DIE("Usage: interface [-n <num>] <filename>\n");
}

int main(int argc, char** argv) {

  /* Stdout is piped to null, so write output to a file */
#ifdef OUTPUT_FILE
  logfile = fopen(LOGFILE, "wb");
  if (logfile == NULL) {
    DIE("Error opening log file for writing\n");
  }
#endif

  /* Parameters */
  const char* filename;
  char* server = DEFAULT_SERVER;
  char* port = DEFAULT_PORT;
  int num = DEFAULT_NUM;

  /* Check num of parameters */
  if (argc < 2)
    printUsageAndDie();

  /* Parse parameters */
  int curArg = 1;
  while (curArg < argc) {
    if (argv[curArg][0] == '-') { //flag
  //     if (argv[curArg][1] == 's') {
  //       // set server
	// server = argv[curArg+1];
	// curArg += 2;
  //     } else if (argv[curArg][1] == 'p') {
  //       // set port
	// port = argv[curArg+1];
	// curArg += 2;
     if (argv[curArg][1] == 'n') {
        // set num
	num = atoi(argv[curArg+1]);
	curArg += 2;
      } else {
        LOG("Unknown flag: %s\n", argv[curArg]);
	printUsageAndDie();
      }
    } else {
      break; // expect filename now
    }
  }
  if (curArg != argc-1)
    printUsageAndDie();
  filename = argv[curArg];
  LOG("input file = %s\n", filename);

  /* Local mode? */
  uint8_t mode = DEFAULT_MODE;
  if (strcmp(server, "localhost") == 0) {
    LOG("Running in LOCAL MODE.\n");
    mode = LOCAL_MODE;
  }

  /* Preamble instrumentation */
  char* shmname = getenv(SHM_ENV_VAR);
  int status = 0;
  uint8_t kelinci_status = STATUS_SUCCESS;
  if (shmname) {

    /* Running in AFL */
    in_afl = 1;
	  
    /* Set up shared memory region */
    LOG("SHM_ID: %s\n", shmname);
    key_t key = atoi(shmname);

    if ((trace_bits = shmat(key, 0, 0)) == (uint8_t*) -1) {
      DIE("Failed to access shared memory 2\n");
    }
    LOGIFVERBOSE("Pointer: %p\n", trace_bits);
    LOG("Shared memory attached. Value at loc 3 = %d\n", trace_bits[3]);

    /* Set up the fork server */
    LOG("Starting fork server...\n");
    if (write(199, &status, 4) != 4) {
      LOG("Write failed\n");
      goto resume;
    }

    while(1) {
      if(read(198, &status, 4) != 4) {
         DIE("Read failed\n");
      }

      int child_pid = fork();
      if (child_pid < 0) {
        DIE("Fork failed\n");
      } else if (child_pid == 0) {
        LOGIFVERBOSE("Child process, continue after pork server loop\n");
	break;
      }

      LOGIFVERBOSE("Child PID: %d\n", child_pid);
      write(199, &child_pid, 4);
      
      LOGIFVERBOSE("Status %d \n", status);

      if(waitpid(child_pid, &status, 0) <= 0) {
        DIE("Fork crash");
      }

      LOGIFVERBOSE("Status %d \n", status);
      write(199, &status, 4);
    }

    resume:
    LOGIFVERBOSE("AFTER LOOP\n\n");
    close(198);
    close(199);

    /* Mark a location to show we are instrumented */
    trace_bits[0]++;

  } else {
    LOG("Not running within AFL. Shared memory and fork server not set up.\n");
    trace_bits = (uint8_t*) malloc(SHM_SIZE);
  }

  /* Done with initialization, now let's start the wrapper! */
  int try = 0;
  size_t nread;
  char buf[FILE_READ_CHUNK];
  FILE *file;
  uint8_t conf = STATUS_DONE;

  // try up to MAX_TRIES time to communicate with the server
  do {
    // if this is not the first try, sleep for 0.1 seconds first
    if(try > 0)
      usleep(100000);

    // setup_tcp_connection(DEFAULT_SERVER, port);

    /* Send mode */
    // write(tcp_socket, &mode, 1);
    uint8_t foo = 0;
    //wait for other apps to start
    for(int index= 1;index<=20;index++)
        if(num>=index)
            wait_others(index-1,servers[index-1], ports[index-1], 1, 0, &foo);
    
    printf("yes\n");
    int signal_send = 4;

    // if (write(tcp_socket, &signal_send, 4) != 4){
    //     DIE("Error sending module signal");
    // }
    for(int index= 1;index<=20;index++)
        if(num>=index){
            if(write(tcp_sockets[index-1], &signal_send, 4) != 4){
                DIE("Error sending module signal");
            }
            LOG("Sent module %d signal: %d\n", index, signal_send);
        }


    /* LOCAL MODE */
    if (mode == LOCAL_MODE) {

      // get absolute path
      char path[10000];
      realpath(filename, path);

      // send path length
      int pathlen = strlen(path);

      for(int index= 1;index<=20;index++)
        if(num>=index){
            if (write(tcp_sockets[index-1], &pathlen, 4) != 4) {
                DIE("Error sending path length");
            }
            LOG("Sent path length: %d\n", pathlen);
        }
      // if (write(tcp_socket, &pathlen, 4) != 4) {
      //   DIE("Error sending path length");
      // }
      // LOG("Sent path length: %d\n", pathlen);

      // send path
      for(int index= 1;index<=20;index++)
        if(num>=index){
            if (write(tcp_sockets[index-1], path, pathlen) != pathlen) {
                DIE("Error sending path");
            }
            LOG("Sent path: %s\n", path);
        }
      // if (write(tcp_socket, path, pathlen) != pathlen) {
      //   DIE("Error sending path");
      // }
      // LOG("Sent path: %s\n", path);

    
    /* DEFAULT MODE */
    } else {

      /* Send file contents */
      file = fopen(filename, "r");
      if (file) {

        // get file size and send
        fseek(file, 0L, SEEK_END);
        int filesize = ftell(file);
        rewind(file);
        LOG("Sending file size %d\n", filesize);

        if (write(tcp_sockets[0], &filesize, 4) != 4) {
          DIE("Error sending filesize");
        }

        // send file bytes
        size_t total_sent = 0;
        while ((nread = fread(buf, 1, sizeof buf, file)) > 0) {
          if (ferror(file)) {
            DIE("Error reading from file\n");
          }
          ssize_t sent = write(tcp_sockets[0], buf, nread);
          total_sent += sent;
          LOG("Sent %lu bytes of %lu\n", total_sent, filesize);
        }
        fclose(file);
      } else {
        DIE("Error reading file %s\n", filename);
      }
    }

    /* Read kelinci_status over TCP */
    for(int index= 1;index<=20;index++)
        if(num>=index){
            nread = read(tcp_sockets[index-1], &kelinci_status, 1);
            if (nread != 1) {
                LOG("Failure reading exit status over socket.\n");
                kelinci_status = STATUS_COMM_ERROR;
                goto cont;
            }
            if(kelinci_status == STATUS_CRASH) crash = 1;
            // LOG("Return kelinci_status = %d\n", status);
        }
    // nread = read(tcp_socket, &kelinci_status, 1);
    // //wait for other apps to finish
    
    // if (nread != 1) {
    //   LOG("Failure reading exit status over socket.\n");
    //   kelinci_status = STATUS_COMM_ERROR;
    //   goto cont;
    // }
    // LOG("Return kelinci_status = %d\n", status);
  
    /* Read "shared memory" over TCP */
    uint8_t *shared_mem = malloc(SHM_SIZE);

    for(int index= 1;index<=20;index++)
        if(num>=index){
            for (int offset = 0; offset < SHM_SIZE; offset += SOCKET_READ_CHUNK) {
                nread = readn(tcp_sockets[index-1], shared_mem+offset, SOCKET_READ_CHUNK);
                if (nread != SOCKET_READ_CHUNK) {
                    LOG("Error reading shm from socket\n");
                    kelinci_status = STATUS_COMM_ERROR;
                    goto cont;
                }
            }

            for (int i = 0; i < SHM_SIZE; i++) {
              if (shared_mem[i] != 0) {
                LOG("%d -> %d\n", i, shared_mem[i]);
                trace_bits[i] += shared_mem[i];
              }
            }
        }
    // for (int offset = 0; offset < SHM_SIZE; offset += SOCKET_READ_CHUNK) {
    //   nread = readn(tcp_socket, shared_mem+offset, SOCKET_READ_CHUNK);
    //   if (nread != SOCKET_READ_CHUNK) {
	  //     LOG("Error reading shm from socket\n");
	  //     kelinci_status = STATUS_COMM_ERROR;
	  //     goto cont;
    //   }
    // }

    /* If successful, copy over to actual shared memory */
    // for (int i = 0; i < SHM_SIZE; i++) {
    //   if (shared_mem[i] != 0) {
    //     LOG("%d -> %d\n", i, shared_mem[i]);
    //     trace_bits[i] += shared_mem[i];
    //     bitmap_size++;
    //   }
    // }

    /* Close socket */
cont: close(tcp_socket);
      for(int index = 1;index<=20;index++){
        if(num>=index)
            close(tcp_sockets[index-1]);
      }

    /* Only try communicating MAX_TRIES times */
    if (try++ > MAX_TRIES) {
      // fail silently...
      DIE("Stopped trying to communicate with server.\n");
    }

  } while (kelinci_status == STATUS_QUEUE_FULL || kelinci_status == STATUS_COMM_ERROR);
    
  LOG("Received results. Terminating.\n\n");

  /* Disconnect shared memory */
  if (in_afl) {
    shmdt(trace_bits);
  }

  /* Terminate with CRASH signal if Java program terminated abnormally */
  if (crash == 1) {
    LOG("Crashing...\n");
    abort();
  }

  /**
   * If JAVA side timed out, keep looping here till AFL hits its time-out.
   * In a good set-up, the time-out on the JAVA process is slightly longer
   * than AFLs time-out to prevent hitting this.
   **/
  if (kelinci_status == STATUS_TIMEOUT) {
    LOG("Starting infinite loop...\n");
    while (1) {
      sleep(10);
    }
  }

  LOG_AND_CLOSE("Terminating normally.\n");

  return 0;
}

