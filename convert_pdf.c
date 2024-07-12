//Server2: 接收pdf文件，调用xpdf的pdftotext将pdf文件转换为txt文件，然后将txt文件发送给Server3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "hooktest.h"

# define BUF_SIZE 1024
# define LISTEN_PORT 8081

# define SERVICE3_IP "127.0.0.1"    //store_txt对应信息
# define SERVICE3_PORT 8082
# define PATH_TO_PDFTOTEXT "/home/gnq/code/pdf2txt/xpdf-3.02/xpdf/pdftotext"
# define OUTPUT_FILE

void error_handling(char *message);
void log_mes(char *message);


int main(){
    int crash = 0;

#ifdef OUTPUT_FILE
    FILE *log_file = fopen("./logs/convert_pdf.log", "a");
    if (log_file == NULL) {
        perror("Failed to open ./logs/read_pdf.log");
        return 1;
    }

    // 将标准输出重定向到文件
    if (dup2(fileno(log_file), fileno(stdout)) == -1) {
        perror("Failed to redirect stdout");
        fclose(log_file);
        return 1;
    }
#endif

    int serv_sock, clnt_sock;
    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t clnt_adr_sz;
    char buffer[BUF_SIZE];

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("socket() error");

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(LISTEN_PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    clnt_adr_sz = sizeof(clnt_adr);
    clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
    if (clnt_sock == -1)
        error_handling("accept() error");

    // 接收pdf文件
    FILE *file = fopen("./tmp/tmpfile.pdf", "wb");
    int read_len;
    while ((read_len = recv(clnt_sock, buffer, BUF_SIZE, 0)) != 0) {
        fwrite(buffer, sizeof(char), read_len, file);
    }
    log_mes("pdf file received");
    
    fclose(file);
    close(clnt_sock);
    close(serv_sock);

    int pid = fork();
    if (pid == 0) { // 子进程
        char *args[] = {PATH_TO_PDFTOTEXT, "./tmp/tmpfile.pdf" ,"./tmp/tmpfile.txt" ,NULL};
        execv(args[0], args); 

        // 如果execv失败，则打印错误并退出
        error_handling("execv error in convert_pdf");
        exit(1);
    } else { // 父进程
        int status;
        waitpid(pid, &status, 0); // 等待子进程结束
        printf("process %d\n",pid);

        if (WIFSIGNALED(status) ) { // 如果子进程因为信号而结束
            int signum = WTERMSIG(status); // 获取导致子进程终止的信号编号
            if(signum != 14){
                printf("child exit with signal %d\n", signum);
                crash = 1;
                fprintf(stderr, "WIFSIGNALED: crash with signal %d\n", signum);
            }
        }else if (WIFEXITED(status)) {
            printf("Child exited with status %d\n", WEXITSTATUS(status));
            fprintf(file, "process %d exited with status %d\n",pid, WEXITSTATUS(status));
        } else {
            printf("Child process did not terminate normally\n");
            fprintf(file, "process %d did not terminate normally\n",pid);
        }
    }

    // 连接到服务3并发送文件
    int sock;
    struct sockaddr_in serv_addr_2;
    char *ip = SERVICE3_IP; // 服务3的IP地址
    int port = SERVICE3_PORT; // 服务3的端口

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr_2, 0, sizeof(serv_addr_2));
    serv_addr_2.sin_family = AF_INET;
    serv_addr_2.sin_addr.s_addr = inet_addr(ip);
    serv_addr_2.sin_port = htons(port);

    
    while(1){
        if (connect(sock, (struct sockaddr *)&serv_addr_2, sizeof(serv_addr_2)) == -1);
        else{
            log_mes("connect to service3(store_txt)");
            break;
        }
    }

    FILE * file2 = fopen("./tmp/tmpfile.txt", "rb");
    while ((read_len = fread(buffer, sizeof(char), 1024, file2)) > 0) {
        send(sock, buffer, read_len, 0);
    }
    fclose(file2);
    close(sock);
#ifdef OUTPUT_FILE
    fclose(log_file);
#endif
    if(crash) abort();
}

void log_mes(char *message){
    printf("[log]: %s\n", message);
}

void error_handling(char *message) {
    printf("[error]:%s\n", message);
    exit(1);
}