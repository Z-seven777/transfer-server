#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define SERVER_PORT 8001
#define BUF_SIZE 4096
#define SAVE_DIR "received_files"

void create_save_directory() {
    system("mkdir -p " SAVE_DIR);
}

int main() {

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    printf("服务器启动...\n");

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 创建失败");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("端口绑定失败");
        return 1;
    }

    listen(server_fd, 5);

    printf("监听端口 %d ...\n", SERVER_PORT);

    create_save_directory();

    while (1) {

        printf("等待客户端连接...\n");

        client_fd = accept(server_fd,
                           (struct sockaddr*)&client_addr,
                           &addr_len);

        if (client_fd < 0) {
            perror("accept 失败");
            continue;
        }

        printf("客户端已连接\n");

        char meta[512] = {0};
        int meta_len = recv(client_fd, meta, sizeof(meta), 0);

        if (meta_len <= 0) {
            printf("接收元信息失败\n");
            close(client_fd);
            continue;
        }

        char file_name[256];
        long file_size = 0;

        sscanf(meta, "%[^|]|%ld", file_name, &file_size);

        printf("准备接收文件: %s (%ld 字节)\n", file_name, file_size);

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

            int recv_bytes = recv(client_fd,
                                  buffer,
                                  (file_size - total_received) > BUF_SIZE ?
                                  BUF_SIZE : (file_size - total_received),
                                  0);

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

        close(client_fd);
    }

    close(server_fd);
    return 0;
}

