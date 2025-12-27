#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

// 现代C++线程池（C++11及以上）
class ThreadPool {
public:
    // 构造函数：指定线程池大小，默认4个线程
    explicit ThreadPool(size_t thread_num = 4) 
        : stop(false) {
        // 创建指定数量的工作线程
        for (size_t i = 0; i < thread_num; ++i) {
            workers.emplace_back([this]() {
                // 工作线程循环：持续从任务队列取任务执行
                while (true) {
                    std::function<void()> task;

                    // 加锁取任务（作用域锁，自动释放）
                    {
                        std::unique_lock<std::mutex> lock(this->mtx);
                        
                        // 等待条件：线程池未停止 且 任务队列为空 → 阻塞
                        this->cv.wait(lock, [this]() {
                            return this->stop || !this->tasks.empty();
                        });

                        // 线程池停止 且 任务队列为空 → 退出线程
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }

                        // 取出队列头部的任务
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    // 执行任务（解锁后执行，避免阻塞其他线程取任务）
                    task();
                }
            });
        }
    }

    // 禁用拷贝构造和赋值（线程池不可拷贝）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 移动构造（可选，现代C++特性）
    ThreadPool(ThreadPool&&) = default;
    ThreadPool& operator=(ThreadPool&&) = default;

    // 析构函数：优雅销毁线程池
    ~ThreadPool() {
        // 标记线程池停止
        {
            std::unique_lock<std::mutex> lock(mtx);
            stop = true;
        }

        // 唤醒所有阻塞的工作线程
        cv.notify_all();

        // 等待所有工作线程退出
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // 提交任务到线程池（核心接口，支持任意参数和返回值）
    // 模板参数：F是任务函数类型，Args是参数类型
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        // 推导任务的返回值类型
        using ReturnType = typename std::result_of<F(Args...)>::type;

        // 将任务封装为std::packaged_task（可获取返回值）
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取任务的future（用于获取返回值）
        std::future<ReturnType> res = task->get_future();

        // 将任务加入队列（加锁保护）
        {
            std::unique_lock<std::mutex> lock(mtx);

            // 线程池已停止 → 拒绝提交任务
            if (stop) {
                throw std::runtime_error("submit task to stopped ThreadPool");
            }

            // 封装为无参函数，加入任务队列
            tasks.emplace([task]() {
                (*task)();
            });
        }

        // 唤醒一个空闲线程执行任务
        cv.notify_one();

        return res;
    }

    // 获取线程池大小（可选）
    size_t size() const {
        return workers.size();
    }

private:
    // 工作线程列表
    std::vector<std::thread> workers;
    // 任务队列（存储无参可调用对象）
    std::queue<std::function<void()>> tasks;
    // 互斥锁：保护任务队列
    std::mutex mtx;
    // 条件变量：通知线程有任务可执行
    std::condition_variable cv;
    // 原子布尔：标记线程池是否停止（避免数据竞争）
    std::atomic<bool> stop;
};