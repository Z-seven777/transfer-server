#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

// 服务器监听端口
#define UPLOAD_PORT 8001   // 发送端上传端口
#define DOWNLOAD_PORT 8002 // 接收端下载端口
// 文件保存目录（服务器本地）
#define SAVE_DIR "/root/jpg_files/"
// 最大文件名长度
#define MAX_FILENAME 256
// 分块传输大小（4KB）
#define BUF_SIZE 4096

// 创建目录（如果不存在）
void create_dir(const char* dir) {
    struct stat st;
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0777);
        printf("创建目录：%s\n", dir);
    }
}

// 处理发送端上传文件
void handle_upload(int client_fd) {
    char filename[MAX_FILENAME] = {0};
    long file_size = 0;
    char save_path[512] = {0};
    FILE* fp = NULL;
    char buffer[BUF_SIZE] = {0};
    int recv_len = 0;
    long total_recv = 0;

    // 1. 接收元信息（文件名|文件大小）
    recv_len = recv(client_fd, filename, sizeof(filename)-1, 0);
    if (recv_len <= 0) {
        printf("接收文件名失败\n");
        close(client_fd);
        return;
    }
    // 解析文件名和大小
    char* sep = strchr(filename, '|');
    if (sep == NULL) {
        printf("元信息格式错误\n");
        close(client_fd);
        return;
    }
    *sep = '\0';
    file_size = atol(sep+1);
    printf("收到上传请求：文件名=%s，大小=%ld字节\n", filename, file_size);

    // 2. 构造保存路径
    snprintf(save_path, sizeof(save_path), "%s%s", SAVE_DIR, filename);

    // 3. 创建文件并接收数据
    fp = fopen(save_path, "wb");
    if (fp == NULL) {
        printf("创建文件失败：%s\n", save_path);
        close(client_fd);
        return;
    }

    // 4. 分块接收文件数据
    while (total_recv < file_size) {
        recv_len = recv(client_fd, buffer, BUF_SIZE, 0);
        if (recv_len <= 0) break;
        fwrite(buffer, 1, recv_len, fp);
        total_recv += recv_len;
    }

    // 5. 校验并关闭
    if (total_recv == file_size) {
        printf("文件上传成功：%s\n", save_path);
    } else {
        printf("文件上传失败，仅接收%ld/%ld字节\n", total_recv, file_size);
        remove(save_path); // 删除不完整文件
    }

    fclose(fp);
    close(client_fd);
}

// 处理接收端下载文件
void handle_download(int client_fd) {
    char filename[MAX_FILENAME] = {0};
    char file_path[512] = {0};
    FILE* fp = NULL;
    long file_size = 0;
    char buffer[BUF_SIZE] = {0};
    int send_len = 0;
    long total_send = 0;

    // 1. 接收要下载的文件名
    recv(client_fd, filename, sizeof(filename)-1, 0);
    printf("收到下载请求：%s\n", filename);

    // 2. 构造文件路径并检查是否存在
    snprintf(file_path, sizeof(file_path), "%s%s", SAVE_DIR, filename);
    fp = fopen(file_path, "rb");
    if (fp == NULL) {
        printf("文件不存在：%s\n", file_path);
        send(client_fd, "FILE_NOT_FOUND", 13, 0);
        close(client_fd);
        return;
    }

    // 3. 获取文件大小
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 4. 发送文件大小（先告诉接收端）
    char size_buf[32] = {0};
    snprintf(size_buf, sizeof(size_buf), "%ld", file_size);
    send(client_fd, size_buf, strlen(size_buf), 0);
    sleep(1); // 避免粘包

    // 5. 分块发送文件数据
    while (total_send < file_size) {
        send_len = fread(buffer, 1, BUF_SIZE, fp);
        if (send_len <= 0) break;
        send(client_fd, buffer, send_len, 0);
        total_send += send_len;
    }

    printf("文件下载完成：%s，发送%ld字节\n", filename, total_send);
    fclose(fp);
    close(client_fd);
}

// 创建TCP监听套接字
int create_listen_socket(int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("创建套接字失败");
        exit(1);
    }

    // 设置端口复用
    int opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    server_addr.sin_port = htons(port);

    // 绑定端口
    if (bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("绑定端口失败");
        exit(1);
    }

    // 开始监听
    if (listen(sock_fd, 5) < 0) {
        perror("监听失败");
        exit(1);
    }

    printf("监听端口 %d 成功\n", port);
    return sock_fd;
}

int main() {
    // 创建文件保存目录
    create_dir(SAVE_DIR);

    // 创建两个监听套接字（上传+下载）
    int upload_sock = create_listen_socket(UPLOAD_PORT);
    int download_sock = create_listen_socket(DOWNLOAD_PORT);

    // 使用select实现多端口监听
    fd_set read_fds;
    int max_fd = (upload_sock > download_sock) ? upload_sock : download_sock;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(upload_sock, &read_fds);
        FD_SET(download_sock, &read_fds);

        // 等待连接
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select失败");
            continue;
        }

        // 处理上传连接
        if (FD_ISSET(upload_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(upload_sock, (struct sockaddr*)&client_addr, &client_len);
            printf("上传客户端连接：%s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            handle_upload(client_fd);
        }

        // 处理下载连接
        if (FD_ISSET(download_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(download_sock, (struct sockaddr*)&client_addr, &client_len);
            printf("下载客户端连接：%s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            handle_download(client_fd);
        }
    }

    close(upload_sock);
    close(download_sock);
    return 0;
}
