#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>

#define SERVER_PORT 8001
#define BUF_SIZE 4096
#define SAVE_DIR "received_files"

void create_save_directory() {
    mkdir(SAVE_DIR, 0755);
}

int recv_all(int sock, void *buf, int len) {
    int total = 0;
    while (total < len) {
        int n = recv(sock, (char*)buf + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

int main() {

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    printf("服务器启动...\n");
    fflush(stdout);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 失败");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind 失败");
        return 1;
    }

    listen(server_fd, 5);

    printf("监听端口 %d ...\n", SERVER_PORT);
    fflush(stdout);

    create_save_directory();

    while (1) {

        printf("等待客户端连接...\n");
        fflush(stdout);

        client_fd = accept(server_fd,
                           (struct sockaddr*)&client_addr,
                           &addr_len);

        if (client_fd < 0) {
            perror("accept 失败");
            continue;
        }

        printf("客户端已连接\n");
        fflush(stdout);

        // 1️⃣ 读取文件大小 (8字节)
        long file_size;
        if (recv_all(client_fd, &file_size, sizeof(long)) < 0) {
            printf("读取文件大小失败\n");
            close(client_fd);
            continue;
        }

        // 2️⃣ 读取文件名长度 (4字节)
        int name_len;
        if (recv_all(client_fd, &name_len, sizeof(int)) < 0) {
            printf("读取文件名长度失败\n");
            close(client_fd);
            continue;
        }

        // 3️⃣ 读取文件名
        char file_name[256] = {0};
        if (recv_all(client_fd, file_name, name_len) < 0) {
            printf("读取文件名失败\n");
            close(client_fd);
            continue;
        }

        printf("准备接收文件: %s (%ld 字节)\n", file_name, file_size);
        fflush(stdout);

        char save_path[512];
        snprintf(save_path, sizeof(save_path),
                 "%s/%s", SAVE_DIR, file_name);

        FILE* fp = fopen(save_path, "wb");
        if (!fp) {
            perror("文件创建失败");
            close(client_fd);
            continue;
        }

        char buffer[BUF_SIZE];
        long total_received = 0;

        while (total_received < file_size) {

            int to_read = (file_size - total_received) > BUF_SIZE ?
                          BUF_SIZE : (file_size - total_received);

            int recv_bytes = recv(client_fd, buffer, to_read, 0);
            if (recv_bytes <= 0) {
                printf("接收中断\n");
                break;
            }

            fwrite(buffer, 1, recv_bytes, fp);
            total_received += recv_bytes;
        }

        fclose(fp);

        if (total_received == file_size) {
            printf("文件接收完成: %s\n", file_name);
        } else {
            printf("文件接收不完整\n");
        }

        fflush(stdout);

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
