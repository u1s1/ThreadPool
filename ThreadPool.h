#ifndef THREAD_POOL
#define THREAD_POOL

#include <queue>
#include <vector>
#include <functional>
#include <future>
#include <condition_variable>
#include <mutex>
#include <memory>

using ThreadPoolTask = std::function<void()>;

class ThreadPool
{
public:
    ThreadPool(int nThreadNum = 4);
    ~ThreadPool();

    // 将线程存入线程池
	template <typename Func, typename... Args>
	auto PushThread(Func &&func, Args &&...args) -> std::future<typename std::result_of<Func(Args...)>::type>;

	// 等待线程池中所有线程运算完毕
    void vWaitAllThreadFinish();

    // 获取线程池中所有线程运算是否完毕
    bool bIsThreadAllDone();

private:
    int m_nThreadWorkingNum; // 正在工作的线程数量
    int m_nTotalThreadNum; // 总线程数量
    bool m_bRunning; // 代表所有线程池是否在运行
    std::queue<ThreadPoolTask> m_queueJob; // 任务队列
    std::condition_variable m_Condition; // 线程等待锁
    std::mutex m_mutexJob; // 为任务加锁防止一个任务被两个线程执行等其他情况
    std::vector<std::thread> m_vecThread; // 所有工作线程的容器

    // 具体工作线程
    void vThreadLoop();
};

inline ThreadPool::ThreadPool(int nThreadNum)
    : m_nTotalThreadNum(nThreadNum)
    , m_nThreadWorkingNum(0)
{
    // 启动线程
    m_bRunning = true;
    // 初始化工作线程
    for (size_t i = 0; i < m_nTotalThreadNum; i++)
    {
        // 为每个工作节点创建一条线程
        m_vecThread.push_back(std::thread(&ThreadPool::vThreadLoop, this));
    }
}

inline ThreadPool::~ThreadPool()
{
    // 关闭管理线程，必须要大括号限制作用域，否则死锁
    {
        std::unique_lock<std::mutex> lock(m_mutexJob);
        m_bRunning = false;
    }
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
inline auto ThreadPool::PushThread(Func&& func, Args&&... args) 
        -> std::future<typename std::result_of<Func(Args...)>::type>
    {
        using ReturnType = typename std::result_of<Func(Args...)>::type;
        using TaskType = std::packaged_task<ReturnType()>;
        
        // 使用 shared_ptr 管理任务
        auto task = std::make_shared<TaskType>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        
        std::future<ReturnType> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_mutexJob);
            m_queueJob.push([task]() { (*task)(); });
        }
        m_Condition.notify_one();
        return res;
    }

inline void ThreadPool::vThreadLoop()
{
    while (m_bRunning)
    {
        ThreadPoolTask job;
        {
            std::unique_lock<std::mutex> lock(m_mutexJob);
            m_Condition.wait_for(lock, std::chrono::milliseconds(5), [this]() { return !m_bRunning || m_queueJob.size() != 0; });
             // 检查worker是否需要结束生命
            if (!m_bRunning)
            {
                break;
            }
            if (m_queueJob.size() != 0)
            {
                 // 获取到job后将该job从任务队列移出，免得其他worker过来重复做这个任务
                job = std::move(m_queueJob.front());
                 // 弹出任务队列
                m_queueJob.pop();
                m_nThreadWorkingNum++;
            }
            else
            {
                lock.unlock();
                m_Condition.notify_all();
                continue;
            }
        }
         // 执行任务
        job();
        std::unique_lock<std::mutex> lock(m_mutexJob);
        m_nThreadWorkingNum--;
        if (m_queueJob.size() == 0)
        {
            lock.unlock();
            m_Condition.notify_all();
        }
    }
}

inline void ThreadPool::vWaitAllThreadFinish()
{
    std::unique_lock<std::mutex> lock(m_mutexJob);
    do
    {
        m_Condition.wait_for(lock, std::chrono::milliseconds(5), [this]() { return !m_bRunning || (m_queueJob.size() == 0 && m_nThreadWorkingNum == 0); });
    } while (m_bRunning && (m_queueJob.size() != 0 || m_nThreadWorkingNum != 0));
    return ;
}

inline bool ThreadPool::bIsThreadAllDone()
{
    std::unique_lock<std::mutex> lock(m_mutexJob);
    return m_queueJob.size() == 0 && m_nThreadWorkingNum == 0;
}

#endif