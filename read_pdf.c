// Service1 读取pdf文件并发送给Service2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVICE2_IP "127.0.0.1"  //convert_pdf对应信息
#define SERVICE2_PORT 8081

// #define OUTPUT_FILE 

void error_handling(char *message);
void log_mes(char *message);

int main(int argc, char *argv[]) {
    char buffer[1024];

    if(argc < 2){
        printf("Usage: ./read_pdf [file]");
        return 1;
    }

    char * file_name = argv[1];
#ifdef OUTPUT_FILE
    FILE *log_file = fopen("./logs/read_pdf.log", "a");
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

    int read_len;
    // 连接到服务2并发送文件
    int sock;
    struct sockaddr_in serv_addr_2;
    char *ip = SERVICE2_IP; // 服务2的IP地址
    int port = SERVICE2_PORT; // 服务2的端口

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr_2, 0, sizeof(serv_addr_2));
    serv_addr_2.sin_family = AF_INET;
    serv_addr_2.sin_addr.s_addr = inet_addr(ip);
    serv_addr_2.sin_port = htons(port);

    log_mes("read_pdf init ok");
    while(1){
        if (connect(sock, (struct sockaddr *)&serv_addr_2, sizeof(serv_addr_2)) == -1);
        else{
            log_mes("connect to service2(convert_pdf)");
            break;
        }
    }

    FILE * file = fopen(file_name, "rb");
    while ((read_len = fread(buffer, sizeof(char), 1024, file)) > 0) {
        send(sock, buffer, read_len, 0);
    }
    fclose(file);
    close(sock);
#ifdef OUTPUT_FILE
    fclose(log_file);
#endif
    return 0;
}

void log_mes(char *message){
    printf("[log]: %s\n", message);
}

void error_handling(char *message) {
    printf("[error]:%s\n", message);
    exit(1);
}