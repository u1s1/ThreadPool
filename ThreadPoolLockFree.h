#ifndef THREAD_POOL_LOCK_FREE
#define THREAD_POOL_LOCK_FREE

#include <queue>
#include <vector>
#include <functional>
#include <future>
#include <condition_variable>
#include <mutex>
#include <memory>
#include "LockFreeQueue.h"

using ThreadPoolTask = std::function<void()>;

class ThreadPoolLockFree
{
public:
    ThreadPoolLockFree(int nThreadNum = 4);
    ~ThreadPoolLockFree();

    // 将线程存入线程池
	template <typename Func, typename... Args>
	auto PushThread(Func &&func, Args &&...args) -> std::future<typename std::result_of<Func(Args...)>::type>;

	// 等待线程池中所有线程运算完毕
    void vWaitAllThreadFinish();

    // 获取线程池中所有线程运算是否完毕
    bool bIsThreadAllDone();

private:
    std::atomic<int> m_nThreadWorkingNum; // 正在工作的线程数量
    int m_nTotalThreadNum; // 总线程数量
    std::atomic<bool> m_bRunning; // 代表所有线程池是否在运行
    std::shared_ptr<LockFreeQueue<ThreadPoolTask>> m_queueJob; // 任务队列
    std::condition_variable m_Condition; // 线程等待锁
    std::mutex m_mutexJob; // 为任务加锁防止一个任务被两个线程执行等其他情况
    std::vector<std::thread> m_vecThread; // 所有工作线程的容器

    // 具体工作线程
    void vThreadLoop();
};

inline ThreadPoolLockFree::ThreadPoolLockFree(int nThreadNum)
    : m_nTotalThreadNum(nThreadNum)
{
    m_queueJob = std::make_shared<LockFreeQueue<ThreadPoolTask>>(nThreadNum * 4);
    m_nThreadWorkingNum.store(0);
    // 启动线程
    m_bRunning.store(true);
    // 初始化工作线程
    for (size_t i = 0; i < m_nTotalThreadNum; i++)
    {
        // 为每个工作节点创建一条线程
        m_vecThread.push_back(std::thread(&ThreadPoolLockFree::vThreadLoop, this));
    }
}

inline ThreadPoolLockFree::~ThreadPoolLockFree()
{
    m_bRunning.store(false);
    m_Condition.notify_all();

    for (size_t i = 0; i < m_nTotalThreadNum; i++)
    {
        if (m_vecThread[i].joinable())
        {
            m_vecThread[i].join();
        }
    }
}

template <typename Func, typename... Args>
inline auto ThreadPoolLockFree::PushThread(Func&& func, Args&&... args) 
        -> std::future<typename std::result_of<Func(Args...)>::type>
    {
        using ReturnType = typename std::result_of<Func(Args...)>::type;
        using TaskType = std::packaged_task<ReturnType()>;
        
        // 使用 shared_ptr 管理任务
        auto task = std::make_shared<TaskType>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        
        std::future<ReturnType> res = task->get_future();
        m_queueJob.get()->push([task]() { (*task)(); });
        m_Condition.notify_one();
        return res;
    }

inline void ThreadPoolLockFree::vThreadLoop()
{
    while (m_bRunning.load())
    {
        std::optional<ThreadPoolTask> job;
        {
            std::unique_lock<std::mutex> lock(m_mutexJob);
            m_Condition.wait_for(lock, std::chrono::milliseconds(5), [this]() { return !m_bRunning.load() || m_queueJob.get()->size() != 0; });
        }
            // 检查worker是否需要结束生命
        if (!m_bRunning.load())
        {
            break;
        }
        // 获取到job后将该job从任务队列移出，免得其他worker过来重复做这个任务
        job = std::move(m_queueJob.get()->pop());
        if (job != std::nullopt)
        {
            m_nThreadWorkingNum.fetch_add(1);
            // 执行任务
            job.value()();
            m_nThreadWorkingNum.fetch_sub(1);
        }
    }
}

inline void ThreadPoolLockFree::vWaitAllThreadFinish()
{
    std::unique_lock<std::mutex> lock(m_mutexJob);
    do
    {
        m_Condition.wait_for(lock, std::chrono::milliseconds(5), [this]() { return !m_bRunning.load() || (m_queueJob.get()->size() == 0 && m_nThreadWorkingNum.load() == 0); });
    } while (m_bRunning.load() && (m_queueJob.get()->size() != 0 || m_nThreadWorkingNum.load() != 0));
    return ;
}

inline bool ThreadPoolLockFree::bIsThreadAllDone()
{
    return m_queueJob.get()->size() == 0 && m_nThreadWorkingNum.load() == 0;
}

#endif