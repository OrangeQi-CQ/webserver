#pragma once
#include <functional>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <vector>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class EpollEventLoop {
public:
    using Callback = std::function<void(const struct epoll_event&)>;

    EpollEventLoop() : epollfd_(-1), quit_(false), MAX_EVENTS_(1024) {
        epollfd_ = epoll_create1(0);
        if (epollfd_ < 0) {
            throw std::runtime_error("epoll_create1 failed: " + std::string(strerror(errno)));
        }
    }

    ~EpollEventLoop() {
        Quit();
        if (epollfd_ >= 0) {
            close(epollfd_);
        }
    }

    EpollEventLoop(const EpollEventLoop&) = delete;
    EpollEventLoop& operator=(const EpollEventLoop&) = delete;
    
    EpollEventLoop(EpollEventLoop&&) = default;
    EpollEventLoop& operator=(EpollEventLoop&&) = default;

    // 添加 fd 到 Epoll 监听（边缘触发 + 非阻塞）
    void AddFd(int fd, uint32_t events) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.data.fd = fd;
        ev.events = events | EPOLLET;  // 边缘触发（高并发首选）
        if (epoll_ctl(epollfd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            throw std::runtime_error("epoll_ctl add fd " + std::to_string(fd) + " failed: " + std::string(strerror(errno)));
        }
    }

    // 从 Epoll 中删除 fd
    void RemoveFd(int fd) {
        if (epoll_ctl(epollfd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
            throw std::runtime_error("epoll_ctl remove fd " +  std::to_string(fd) + " failed: " + std::string(strerror(errno)));
        }
    }

    void SetCallback(Callback cb) {
        call_back_ = cb;
    }

    void Loop() {    
        std::vector<struct epoll_event> events(MAX_EVENTS_);
        quit_ = false;

        while (!quit_) {
            // 阻塞等待就绪事件（-1 = 无限阻塞，信号中断则重试）
            int nfds = epoll_wait(epollfd_, events.data(), MAX_EVENTS_, -1);
            if (nfds < 0) {
                if (errno == EINTR) {
                    continue;  // 信号中断，继续循环
                }
                throw std::runtime_error("epoll_wait failed: " + std::string(strerror(errno)));
            }

            // 分发就绪事件
            for (int i = 0; i < nfds; ++i) {
                call_back_(events[i]);
            }
        }
    }

    // 停止事件循环
    void Quit() {
        quit_ = true;
    }
    
private:
    int epollfd_;
    bool quit_;
    int MAX_EVENTS_;

    Callback call_back_;
};