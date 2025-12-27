#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <unordered_map>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "thread_pool.hpp"
#include "epoll_event_loop.hpp"

// RAII 封装的 sererfd 和 eventloop
class WebServerSocket {
public:
    explicit WebServerSocket(
        int port, 
        std::weak_ptr<EpollEventLoop> event_loop, 
        bool reuse_port = true, 
        bool non_blocking = true, 
        int backlog = 128): port_(port), event_loop_(event_loop) 
    {
        // 初始化服务器 socket
        Socket(reuse_port, non_blocking);
        Bind();
        Listen(backlog);

        // 注册到 Epoll 事件循环
        auto event_loop_ptr = GetEventLoop();
        event_loop_ptr->AddFd(serverfd_, EPOLLIN);   
    }

    WebServerSocket(const WebServerSocket&) = delete;
    WebServerSocket& operator=(const WebServerSocket&) = delete;

    ~WebServerSocket() {
        if (serverfd_ >= 0) {
            close(serverfd_);
        }
        auto event_loop_ptr = GetEventLoop();
        event_loop_ptr->RemoveFd(serverfd_);
    }

    int GetServerFd() const {
        return serverfd_;
    }
     
private:
    std::shared_ptr<EpollEventLoop> GetEventLoop() const {
        auto it = event_loop_.lock();
        if (!it) {
            throw std::runtime_error("EpollEventLoop is expired");
        }
        return it;
    }

    void Socket(bool reuse_port, bool non_blocking) {
        serverfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (serverfd_ < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        if (reuse_port) {
            int opt = 1;
            setsockopt(serverfd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
        }

        if (non_blocking) {
            int flags = fcntl(serverfd_, F_GETFL, 0);
            if (flags != -1) {
                fcntl(serverfd_, F_SETFL, flags | O_NONBLOCK);
            }
        }
    }

    void Bind() {
        std::memset(&serveraddr_, 0, sizeof(serveraddr_));
        serveraddr_.sin_family = AF_INET;
        serveraddr_.sin_addr.s_addr = INADDR_ANY;
        serveraddr_.sin_port = htons(port_);

        if (bind(serverfd_, (struct sockaddr*)&serveraddr_, sizeof(serveraddr_)) < 0) {
            throw std::runtime_error("Bind failed");
        }
    }

    void Listen(int backlog) {
        if (listen(serverfd_, backlog) < 0) {
            close(serverfd_);
            throw std::runtime_error("Listen failed");
        }
    }

    const int port_;
    std::weak_ptr<EpollEventLoop> event_loop_;
    int serverfd_ = -1;
    struct sockaddr_in serveraddr_;
};

/**
 * @brief 封装已连接的客户端socket，用 RAII 管理 client 的 epoll 注册和关闭
 */
class ClientSocket {
public:
    // 新增构造函数：接收已通过accept4获取的client_fd
    ClientSocket(int client_fd, struct sockaddr_in client_addr, std::weak_ptr<EpollEventLoop> event_loop) 
            : client_fd_(client_fd), client_addr_(client_addr), event_loop_(event_loop) {
        // 设置非阻塞
        int flags = fcntl(client_fd_, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("fcntl F_GETFL failed");
        }
        if (fcntl(client_fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::runtime_error("fcntl F_SETFL failed");
        }

        // 注册到 Epoll 事件循环
        auto event_loop_ptr = GetEventLoop();
        event_loop_ptr->AddFd(client_fd_, EPOLLIN);
    }

    ~ClientSocket() {
        auto event_loop_ptr = GetEventLoop();
        event_loop_ptr->RemoveFd(client_fd_);
        if (client_fd_ >= 0) {
            close(client_fd_);
        }
    }

    std::string GetClientIP() const {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr_.sin_addr, ip_str, sizeof(ip_str));
        return std::string(ip_str);
    }
    
    uint16_t GetClientPort() const {
        return ntohs(client_addr_.sin_port);
    }

private:
    std::shared_ptr<EpollEventLoop> GetEventLoop() const {
        auto it = event_loop_.lock();
        if (!it) {
            throw std::runtime_error("EpollEventLoop is expired");
        }
        return it;
    }
    
    struct sockaddr_in client_addr_;
    std::weak_ptr<EpollEventLoop> event_loop_;
    int client_fd_ = -1;
};

class WebServer {
public:
    WebServer(int port, size_t thread_pool_size): port(port), thread_pool_(thread_pool_size) {
        event_loop_ = std::make_shared<EpollEventLoop>();
        auto event_loop_weak = std::weak_ptr<EpollEventLoop>(event_loop_);
        server_socket_ = std::make_unique<WebServerSocket>(port, event_loop_weak);
        event_loop_->SetCallback(
            [this](const struct epoll_event& event) {
                this->HandleEpollEvent(event);
            }
        );
    }
    
    ~WebServer() {}

    void start() {
        event_loop_->Loop();
    }
private:
    int handle_client(int clientfd, const std::string& client_ip, uint16_t client_port) {
        
        // 1. 读取客户端请求（不解析，仅读取）
        ssize_t total_read = 0;
        const int BUFFER_SIZE = 1024;
        char buffer[BUFFER_SIZE] = {0};
        while (true) {
            ssize_t read_bytes = read(clientfd, buffer + total_read, BUFFER_SIZE - 1 - total_read);
            if (read_bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // 数据读取完毕，退出循环
                } else {
                    // 处理真错误
                    return -1;
                }
            } else if (read_bytes == 0) {
                return -1; // 客户端关闭连接
            }
            total_read += read_bytes;
            if (total_read >= BUFFER_SIZE - 1) {
                break; // 缓冲区满，退出循环
            }
        }

        // 2. 构造标准HTTP响应
        const char* response_body = "<h1>Hello, ThreadPool WebServer!</h1>";
        int body_len = static_cast<int>(strlen(response_body));
        char response[1024] = {0};
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Server: ModernCppThreadPool/1.0\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"  // 关键修改：改为短连接，告诉客户端处理完后关闭连接
            "\r\n"
            "%s",
            body_len, response_body);

        // 3. 发送响应
        ssize_t total_sent = 0;
        ssize_t response_len = strlen(response);
        const char* response_ptr = response;

        while (total_sent < response_len) {
            ssize_t sent_bytes = write(clientfd, response_ptr + total_sent, response_len - total_sent);
            if (sent_bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 发送缓冲区满，短暂休眠后重试（避免CPU空转）
                    usleep(100);
                    continue;
                } else {
                    // 真错误（如连接关闭），返回-1清理连接
                    close(clientfd); // 主动关闭无效fd
                    return -1;
                }
            } else if (sent_bytes == 0) {
                break; // 连接已关闭，退出循环
            }
            total_sent += sent_bytes;
        }

        return 0;
    }

    void HandleEpollEvent(const struct epoll_event& event) {
        int revent = event.events;
        int fd = event.data.fd;

        // 优先处理错误/挂断事件（解决无效fd堆积）
        if (revent & (EPOLLERR | EPOLLHUP)) {
            std::lock_guard<std::mutex> lock(client_sockets_mutex_);
            client_sockets_.erase(fd); // 自动销毁ClientSocket，关闭fd并从Epoll移除
            return;
        }
        
        // 新连接事件
        if ((revent & EPOLLIN) && fd == server_socket_->GetServerFd()) {
            HandleNewConnection();
        } 

        // 已连接客户端的读事件
        if ((revent & EPOLLIN) && fd != server_socket_->GetServerFd()) {
            HandleReadEvent(fd);
        }
    }

    void HandleNewConnection() {
        int server_fd = server_socket_->GetServerFd();

        // 边缘触发模式下，循环accept4接收所有待处理连接
        while (true) {
            socklen_t client_addr_len = sizeof(sockaddr_in);
            struct sockaddr_in client_addr;
            int client_fd = accept4(server_fd, (struct sockaddr*)&client_addr, &client_addr_len, SOCK_NONBLOCK);
            if (client_fd < 0) {
                break;
            }
            auto client = std::make_unique<ClientSocket>(client_fd, client_addr, this->event_loop_);
            std::string client_ip = client->GetClientIP();
            uint16_t client_port = client->GetClientPort();
            std::lock_guard<std::mutex> lock(client_sockets_mutex_);
            client_sockets_[client_fd] = std::move(client);
        }
    }

    void HandleReadEvent(int clientfd) {
        std::string client_ip;
        uint16_t client_port;
        {
            std::lock_guard<std::mutex> lock(client_sockets_mutex_);
            auto it = client_sockets_.find(clientfd);
            if (it == client_sockets_.end()) {
                return;
            }
            auto client = it->second.get();
            // 解析客户端信息
            client_ip = client->GetClientIP();
            client_port = client->GetClientPort();
        }

        // 提交到线程池处理
        auto task = [this, clientfd, client_ip, client_port]() -> int {
            int ret = this->handle_client(clientfd, client_ip, client_port);
            // 处理完成后清理 ClientSocket
            std::lock_guard<std::mutex> lock(this->client_sockets_mutex_);
            this->client_sockets_.erase(clientfd);
            return ret;
        };
        thread_pool_.submit(task);
    }

    const int port;
    std::shared_ptr<EpollEventLoop> event_loop_;
    std::unique_ptr<WebServerSocket> server_socket_;
    ThreadPool thread_pool_;
    std::unordered_map<int, std::unique_ptr<ClientSocket>> client_sockets_;
    std::mutex client_sockets_mutex_;
};