#ifndef THREAD_POOL
#define THREAD_POOL
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <time.h>
#include <utility>
#include <vector>


#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <time.h>
#include <utility>
#include <vector>

template <typename T> struct safe_queue {
    std::queue<T> que;
    std::shared_mutex _m;
    bool empty() {
        std::shared_lock<std::shared_mutex> lc(_m);
        return que.empty();
    }
    auto size() {
        std::shared_lock<std::shared_mutex> lc(_m);
        return que.size();
    }
    void push(T& t) {
        std::unique_lock<std::shared_mutex> lc(_m);
        que.push(t);
    }
    bool pop(T& t) {
        std::unique_lock<std::shared_mutex> lc(_m);
        if (que.empty())
            return false;
        t = std::move(que.front());
        que.pop();
        return true;
    }
};
class ThreadPool {
  private:
    class worker {
      public:
        ThreadPool* pool;
        worker(ThreadPool* _pool) : pool{_pool} {}

        void operator()() {
            while (!pool->is_shut_down) {
                {
                    std::unique_lock<std::mutex> lock(pool->_m);
                    pool->cv.wait(lock, [this]() { return this->pool->is_shut_down || !this->pool->que.empty(); });
                }
                std::function<void()> func;
                bool flag = pool->que.pop(func);
                if (flag) {
                    func();
                }
            }
        }
    };

  public:
    bool is_shut_down;
    safe_queue<std::function<void()>> que;
    std::vector<std::thread> threads;
    std::mutex _m;
    std::condition_variable cv;
    ThreadPool(int n) : threads(n), is_shut_down{false} {
        for (auto& t : threads)
            t = std::thread{worker(this)};
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    static ThreadPool& getInstance() {
        // static ThreadPool instance(std::thread::hardware_concurrency());
        static ThreadPool instance(64);
        return instance;
    }

    template <typename F, typename... Args> auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        std::function<decltype(f(args...))()> func = [&f, args...]() { return f(args...); };
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);
        std::function<void()> warpper_func = [task_ptr]() { (*task_ptr)(); };
        que.push(warpper_func);
        cv.notify_one();
        return task_ptr->get_future();
    }
    
    ~ThreadPool() {
        auto f = submit([]() {});
        f.get();
        is_shut_down = true;
        cv.notify_all();  // 通知，唤醒所有工作线程
        for (auto& t : threads) {
            if (t.joinable())
                t.join();
        }
    }
};
#endif