#ifndef LOCK_FREE_QUEUE
#define LOCK_FREE_QUEUE

#include<atomic>
#include<thread>
#include <optional>
#include "HazardPoint.h"

template <typename T>
class LockFreeQueue
{
private:
    struct QueueNode
    {
        std::optional<T> data;
        std::atomic<QueueNode *> next;
        QueueNode() : data(std::nullopt), next(nullptr) {}
        explicit QueueNode(T const& value) : data(value), next(nullptr) {}
        explicit QueueNode(T&& value) : data(std::move(value)), next(nullptr) {}
    };

public:
    LockFreeQueue(int nHazardPointSize = 16);
    ~LockFreeQueue();

    std::optional<T> pop();

    void push(const T &value);
    void push(T &&value);
    int size();
    void clear();

private:
    void AddToDeleteWaitQueue(QueueNode *waitDeletePointer);
    void DeleteWaitQueue();

    std::atomic<QueueNode *> m_head;
    std::atomic<QueueNode *> m_tail;
    std::atomic<QueueNode *> m_deleteWaitHead;
    std::atomic<int> m_size;
    std::shared_ptr<HazardPointManager> m_hazardPointManager;
    std::atomic<int> m_deleteWaitCount;
};

template <typename T>
inline  LockFreeQueue<T>::LockFreeQueue(int nHazardPointSize)
{
    QueueNode *dummy = new QueueNode();
    m_head.store(dummy, std::memory_order_release);
    m_tail.store(dummy, std::memory_order_release);
    m_deleteWaitHead.store(nullptr, std::memory_order_release);
    m_size.store(0, std::memory_order_release);
    m_hazardPointManager = std::make_shared<HazardPointManager>(nHazardPointSize * 2);
    m_deleteWaitCount.store(0, std::memory_order_release);
}

template <typename T>
inline  LockFreeQueue<T>::~LockFreeQueue()
{
    while (pop().has_value());
    QueueNode* nDelete = m_head.load(std::memory_order_relaxed);
    if (nDelete) delete nDelete;

    nDelete = m_deleteWaitHead.exchange(nullptr, std::memory_order_acq_rel);
    QueueNode *nextDelete;
    while (nDelete)
    {
        nextDelete = nDelete->next.load(std::memory_order_relaxed);
        delete nDelete;
        nDelete = nextDelete;
    }
}

template <typename T>
inline std::optional<T> LockFreeQueue<T>::pop()
{
    //获取当前线程下的风险指针
    HazardPoint *thisThreadHazardPoint = m_hazardPointManager->GetHazardPoint();
    if (thisThreadHazardPoint == nullptr)
    {
        return std::nullopt;
    }
    //需要保护的当前头节点，此节点内已无可用数据，pop后被删除
    QueueNode *oldHead = m_head.load(std::memory_order_acquire);
    //需要保护的当前头节点，此节点内已无可用数据，用于放入风险数组
    QueueNode *tempNode;
    //头节点的下一个节点，存放着真正需要返回的数据，pop后此节点被置为新的头节点
    QueueNode *returnNode;
    std::optional<T> dataOptional = std::nullopt;

    //外层do-while用于更新队列头
    do
    {
        //内层do-while用于更新风险指针
        do
        {
            //更新头节点数据到此处
            tempNode = m_head.load(std::memory_order_acquire);
            returnNode = tempNode->next.load(std::memory_order_acquire);
            //更新风险指针
            thisThreadHazardPoint->hazardStorePoint[0].store((void *)tempNode, std::memory_order_release);
            thisThreadHazardPoint->hazardStorePoint[1].store((void *)returnNode, std::memory_order_release);
            //获取最新头位置
            oldHead = m_head.load(std::memory_order_acquire);
        //比较最新头位置和风险数组内的是不是同一个
        } while (tempNode != oldHead);
    //若要获取数据的真正节点是空则跳出，或者成功更新头节点到要获取数据的真正节点也跳出
    } while (returnNode != nullptr && !m_head.compare_exchange_strong(oldHead, returnNode,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_relaxed));

    if (returnNode != nullptr)
    {
        dataOptional = std::move(returnNode->data);
        returnNode->data.reset();
        //释放此线程持有的风险指针
        thisThreadHazardPoint->hazardStorePoint[0].store(nullptr, std::memory_order_release);
        thisThreadHazardPoint->hazardStorePoint[1].store(nullptr, std::memory_order_release);
        //查看其它线程是否持有此指针
        if (m_hazardPointManager->IsConflictPoint((void *)oldHead))
        {
            //加入待删除队列
            AddToDeleteWaitQueue(oldHead);
        }
        else
        {
            delete oldHead;
        }
        m_size.fetch_sub(1);
    }
    else
    {
        //释放此线程持有的风险指针
        thisThreadHazardPoint->hazardStorePoint[0].store(nullptr, std::memory_order_release);
        thisThreadHazardPoint->hazardStorePoint[1].store(nullptr, std::memory_order_release);
    }
    //清空待删除队列
    if (m_deleteWaitCount.fetch_add(1, std::memory_order_release) > 32)
    {
        DeleteWaitQueue();
        m_deleteWaitCount.store(0, std::memory_order_release);
    }

    return dataOptional;
}

template <typename T>
inline void LockFreeQueue<T>::push(const T &value)
{
    push(T(value));
}

template <typename T>
inline void LockFreeQueue<T>::push(T &&value)
{
    QueueNode *newNode = new QueueNode(std::move(value));
    newNode->next.store(nullptr, std::memory_order_release);
    QueueNode *oldTail;
    QueueNode *oldTailNext;
    while(true)
    {
        oldTail = m_tail.load(std::memory_order_acquire);
        oldTailNext = oldTail->next.load(std::memory_order_acquire);
        if (oldTailNext != nullptr)
        {
            //帮助其他线程推进尾指针
            m_tail.compare_exchange_weak(oldTail, oldTailNext);
            continue;
        }
        //更新next指针为新节点
        if(oldTail->next.compare_exchange_strong(oldTailNext, newNode,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_relaxed))
        {
            //将尾指针后移
            m_tail.compare_exchange_weak(oldTail, newNode);
            break;
        }
    }
    m_size.fetch_add(1);
}

template <typename T>
inline int LockFreeQueue<T>::size()
{
    return m_size.load(std::memory_order_acquire);
}

template <typename T>
inline void LockFreeQueue<T>::clear()
{
    while (pop() != nullptr);
    DeleteWaitQueue();
}

template <typename T>
inline void LockFreeQueue<T>::AddToDeleteWaitQueue(QueueNode *waitDeletePointer)
{
    QueueNode *oldWaitDeleteHead;
    do
    {
        oldWaitDeleteHead = m_deleteWaitHead.load(std::memory_order_acquire);
        waitDeletePointer->next.store(oldWaitDeleteHead, std::memory_order_release);
    } while (!m_deleteWaitHead.compare_exchange_strong(oldWaitDeleteHead, waitDeletePointer,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_relaxed));
}

template <typename T>
inline void LockFreeQueue<T>::DeleteWaitQueue()
{
    QueueNode *waitDeleteHead = m_deleteWaitHead.exchange(nullptr);
    QueueNode *waitDeleteHeadNext;
    while (waitDeleteHead != nullptr)
    {
        waitDeleteHeadNext = waitDeleteHead->next.load(std::memory_order_acquire);
        if (m_hazardPointManager->IsConflictPoint((void*)waitDeleteHead))
        {
            AddToDeleteWaitQueue(waitDeleteHead);
        }
        else
        {
            delete waitDeleteHead;
        }
        waitDeleteHead = waitDeleteHeadNext;
    }
}

#endif