#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h> 

#define TMPFILE "/home/gnq/tmpfile"

#define FILE_READ_CHUNK 1024
#define SHM_SIZE 65536
#define SHM_ENV_VAR "__AFL_SHM_ID"


#define STATUS_SUCCESS 0
#define STATUS_TIMEOUT 1
#define STATUS_CRASH 2
#define STATUS_COMM_ERROR 3
#define STATUS_DONE 4

#define IPC_CREAT	01000		/* Create key if key does not exist. */
#define IPC_EXCL	02000		/* Fail if key exists.  */
#define IPC_NOWAIT	04000		/* Return error on wait.  */
#define IPC_PRIVATE	((__key_t) 0)	/* Private key.  */

#define alloc_printf(_str...)                        \
    ({                                                 \
                                                       \
      uint8_t *_tmp;                                        \
      int32_t _len = snprintf(NULL, 0, _str);              \
      if (_len < 0) fprintf(stderr, "Whoa, snprintf() fails?!"); \
      _tmp = malloc(_len + 1);                       \
      snprintf((char *)_tmp, _len + 1, _str);          \
      _tmp;                                            \
                                                       \
    })

int shm_setup(){
    int shm_id = shmget(IPC_PRIVATE, SHM_SIZE, IPC_CREAT | IPC_EXCL | 0600);
    char* shm_str = alloc_printf("%d", shm_id);
    if( setenv(SHM_ENV_VAR, shm_str, 1) != 0){
        fprintf(stderr, "setenv SHM_ENV_VAR error");
        return -1;
    }
    char * _map_size_str = alloc_printf("%d", SHM_SIZE);
    if( setenv("AFL_MAP_SIZE", _map_size_str , 1) !=0){
        fprintf(stderr, "setenv AFL_MAP_SIZE error");
        return -1;
    }
    return 0;
}

uint8_t client_status = STATUS_SUCCESS;
int crash = 0;
int main(int argc, char *argv[]) {
    int round = 0;
    FILE *file;
    pid_t pid = getpid();
    // 打开文件，模式为写入（"a"）
    file = fopen("fuzz.log", "a");
    
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <command to run a component> [-f]\n -f is added if this component need a file as input\n", argv[0]);
        exit(1);
    }

    int PORT = atoi(argv[1]);
    if (PORT <= 0 || PORT > 65535) {
        fprintf(stderr, "Invalid port number.\n");
        exit(1);
    }
    char *command = argv[2];
    int need_file = 0;
    if (argc >= 4) {
        if (strcmp(argv[3], "-f") == 0){
            need_file = 1;
            printf("File input\n");
        }
        else{
            printf("Usage: %s <port> <command to run a component> [-f]\n -f is added if this component need a file as input\n", argv[0]);
            exit(1);
        }
    }

    int afl_ret = shm_setup();
    if( afl_ret != 0){
        fprintf(stderr, "shm_setup error");
        exit(1);
    }

    /* 初始化覆盖率共享内存块 */
    uint8_t* trace_bits ;
    char *id_str = getenv(SHM_ENV_VAR);
    int   shm_id;
    if (id_str) {
        shm_id = atoi(id_str);
        trace_bits = (unsigned char *)shmat(shm_id, NULL, 0);
        if (trace_bits == (void *)-1) {
            fprintf(stderr, "get shm error");
        }
        memset(trace_bits, 0, SHM_SIZE);
    } 
    uint8_t *shared_mem = malloc(SHM_SIZE);


    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    char send_data = 1; // 创建一个值为1的字节
    while(1){
        // 创建socket
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(1);
        }
        int optval = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));


        // 设置地址和端口信息
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        server_addr.sin_addr.s_addr = INADDR_ANY;
        memset(&(server_addr.sin_zero), '\0', 8);

        // 绑定地址到socket
        if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
            perror("bind");
            exit(1);
        }

        // 开始监听端口
        if (listen(server_fd, 1) == -1) {
            perror("listen");
            exit(1);
        }

        printf("waiting\n");

        // 接受连接
        addr_len = sizeof(struct sockaddr_in);
        if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len)) == -1) {
            perror("accept");
            goto cont;
        }

        printf("Received connection\n");
    
        // 发送一个字节的数据到客户端
        ssize_t bytes_sent = send(client_fd, &send_data, sizeof(send_data), 0);
        if (bytes_sent < 0) {
            perror("send");
            goto cont;
        }
        int signal_received;
        int first_time_down = 1;
        if (read(client_fd, &signal_received, 4) != 4) {
            perror("Error receiving module signal");
            goto cont;
        }
        first_time_down = 1;
        printf("Read signal successfully!\n");

        if (signal_received != 4) {
            fprintf(stderr, "Incorrect signal received\n");
            continue;
        }
        printf("Signal correct!\n");


        //接收文件
        if(need_file){
            // Receive file size
            int filesize;
            FILE *file = fopen(TMPFILE, "w");
            char buf[1024];
        
            if(read(client_fd, &filesize, 4) != 4) {
                perror("Error receiving filesize\n");
                client_status = STATUS_COMM_ERROR;
            }

            printf("Received file size: %d\n", filesize);

            // Open a file for writing
            if (!file) {
                perror("Error opening file for writing\n");
            }
            // Receive file bytes
            size_t total_received = 0;
            while (total_received < filesize) {
                ssize_t nread = read(client_fd, buf, FILE_READ_CHUNK);
                if (nread <= 0) {
                    perror("Error receiving file content\n");
                    client_status = STATUS_COMM_ERROR;
                    break;
                }
                total_received += nread;
                fwrite(buf, 1, nread, file);
            }
            printf("Received %lu bytes of %d\n", total_received, filesize);
            fclose(file);
        }

        memset(trace_bits, 0, SHM_SIZE);
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        // 循环
        if (pid == 0) { // 子进程
            
            if (need_file){
                char *child_args[] = {command, TMPFILE, NULL};
                execv(child_args[0], child_args); 
            }
            else
            {
                char *child_args[] = {command, NULL};
                execv(child_args[0], child_args); 
            }

            // 如果execv失败，则打印错误并退出
            perror("execv");
            exit(1);
        } else { // 父进程
            int status;
            waitpid(pid, &status, 0); // 等待子进程结束
            fprintf(file, "process %d fuzzed round %d\n",pid,round);
            round++;

            if (WIFSIGNALED(status) ) { // 如果子进程因为信号而结束
                int signum = WTERMSIG(status); // 获取导致子进程终止的信号编号
                printf("child exit with signal %d\n", signum);
                client_status = STATUS_CRASH;
                crash = 1;
                perror("WIFSIGNALED: ====crash=======");
            }else if (WIFEXITED(status)) {
                client_status = STATUS_SUCCESS;
                printf("Child exited with status %d\n", WEXITSTATUS(status));
                fprintf(file, "process %d exited with status %d\n",pid, WEXITSTATUS(status));
            } else {
                printf("Child process did not terminate normally\n");
                fprintf(file, "process %d did not terminate normally\n",pid);
            }
            fflush(file);   
        }

        if(write(client_fd, &client_status, 1) != 1){    
            fprintf(stderr,"write_status_error\n");
            client_status = STATUS_COMM_ERROR;
        }else{
            printf("write_status_ok\n");
        }

        memset(shared_mem, 0, SHM_SIZE);
        // 将数据从实际共享内存复制到共享缓冲区
        memcpy(shared_mem, trace_bits, SHM_SIZE);    
        int bitmap_size = 0;
        for (int i = 0; i < SHM_SIZE; i++) {
            if(shared_mem[i] != 0){
                bitmap_size++;
            }
        }
        printf("bitmap_size: %d\n", bitmap_size);

        //将数据分块写入套接字
        size_t total_write = 0;
        ssize_t nwrite;
        while (total_write < SHM_SIZE) {
            nwrite = send(client_fd, shared_mem+total_write, FILE_READ_CHUNK, MSG_NOSIGNAL);
            if (nwrite < 0) {
                perror("Error send shm to tcp\n");
                client_status = STATUS_COMM_ERROR;
                break;
            }
            total_write += nwrite;
        }
        cont:
        close(client_fd);
        close(server_fd);
    }

    shmdt(trace_bits);
    fclose(file);
    if(shared_mem != NULL)
        free(shared_mem);
    return 0;
}
