#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

const int PORT = 8080;

const size_t BUFFER_SIZE = 1024;

int main() {
    int serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;  
    }

    int opt = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in serveraddr;
    std::memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = INADDR_ANY;
    serveraddr.sin_port = htons(PORT);

    if (bind(serverfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(serverfd);
        return 1;  
    }

    if (listen(serverfd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(serverfd);
        return 1;  
    }

    while (true) {
        struct sockaddr_in clientaddr;
        socklen_t clientlen = sizeof(clientaddr);
        int clientfd = accept(serverfd, (struct sockaddr*)&clientaddr, &clientlen);
        if (clientfd < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue;  
        }

        char buffer[BUFFER_SIZE] = {0};
        read(clientfd, buffer, BUFFER_SIZE);

        const char* response_body = "<h1>Hello, Linux WebServer!</h1>";
        // 动态计算响应体长度，避免硬编码错误
        int body_len = strlen(response_body);
        char response[1024] = {0};
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Server: SimpleCppServer/1.0\r\n"  // 新增：服务器标识（可选）
            "Content-Type: text/html; charset=utf-8\r\n"  // 新增：字符编码
            "Content-Length: %d\r\n"           // 动态填充长度
            "Connection: close\r\n"            // 新增：告知客户端关闭连接
            "\r\n"                             // 空行（必须）
            "%s",                              // 响应体
            body_len, response_body);
        write(clientfd, response, strlen(response));
        close(clientfd);
    }

    close(serverfd);
    return 0;
}