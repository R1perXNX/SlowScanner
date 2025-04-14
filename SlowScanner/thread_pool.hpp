#pragma once
#include <iostream>
#include <future>
#include <thread>
#include <mutex>
#include <queue>

class thread_pool {
public:
    __forceinline explicit thread_pool(size_t num_threads)
        : stop(false)
    {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] {
                            return stop || !tasks.empty();
                            });
                        if (stop && tasks.empty())
                            return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
                });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    __forceinline ~thread_pool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

template<class F, class ...Args>
inline auto thread_pool::enqueue(F&& f, Args && ...args) -> std::future<typename std::invoke_result<F, Args ...>::type>
{
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [fn = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
            return fn(std::move(args)...);
        }
    );

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock lock(queue_mutex);
        if (stop)
            return result; // safe fallback: return future, but task won't run
        tasks.emplace([task]() { (*task)(); });
    }

    condition.notify_one();
    return result;
    
}
