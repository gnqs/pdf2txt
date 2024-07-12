// Service3.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024
#define LISTEN_PORT 8082
#define OUTPUT_FILE

void error_handling(char *message);
void log_mes(char *message);

int main() {

#ifdef OUTPUT_FILE
    FILE *log_file = fopen("./logs/store_txt.log", "a");
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
    log_mes("get connection");

    // 接收TXT文件
    FILE *file = fopen("./tmp/saved.txt", "wb");
    int read_len;
    while ((read_len = recv(clnt_sock, buffer, BUF_SIZE, 0)) != 0) {
        fwrite(buffer, sizeof(char), read_len, file);
    }
    fclose(file);
    log_mes("receive file");
    close(clnt_sock);
    close(serv_sock);

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