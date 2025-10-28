#ifndef LOCK_FREE_QUEUE
#define LOCK_FREE_QUEUE

#include<atomic>
#include<thread>
#include "HazardPoint.h"

template <typename T>
class LockFreeQueue
{
private:
    struct QueueNode
    {
        std::shared_ptr<T> data;
        QueueNode* next;
        QueueNode() : data(nullptr), next(nullptr) {}
    };

public:
    LockFreeQueue(int nHazardPointSize = 8);
    ~LockFreeQueue();

    std::shared_ptr<T> pop();

    void push(const T &value);
    void push(T &&value);
    int size();

private:
    void AddToDeleteWaitQueue(QueueNode *waitDeletePointer);
    void DelteWaitQueue();

    std::atomic<QueueNode *> m_head;
    std::atomic<QueueNode *> m_tail;
    std::atomic<QueueNode *> m_deleteWaitHead;
    std::atomic<int> m_size;
    std::shared_ptr<HazardPointManager> m_hazardPointManager;
};

template <typename T>
inline  LockFreeQueue<T>::LockFreeQueue(int nHazardPointSize)
{
    QueueNode *dummy = new QueueNode();
    m_head.store(dummy);
    m_tail.store(dummy);
    m_deleteWaitHead.store(nullptr);
    m_size.store(0);
    m_hazardPointManager = std::make_shared<HazardPointManager>(nHazardPointSize);
}

template <typename T>
inline  LockFreeQueue<T>::~LockFreeQueue()
{
    while (pop() != nullptr);
    delete m_tail.load();
}

template <typename T>
inline std::shared_ptr<T> LockFreeQueue<T>::pop()
{
    if (m_size.load() == 0)
    {
        return nullptr;
    }
    
    //获取当前线程下的风险指针
    HazardPoint *thisThreadHazardPoint = m_hazardPointManager->GetHazardPoint();
    QueueNode *oldHead = m_head.load();
    QueueNode *tempNode;
    std::shared_ptr<T> dataPointer = nullptr;

    //外层do-while用于更新队列头
    do
    {
        //内层do-while用于更新风险指针
        do
        {
            tempNode = oldHead;
            thisThreadHazardPoint->hazardStorePoint.store((void *)tempNode);
            oldHead = m_head.load();
        } while (tempNode != oldHead);
    } while (oldHead != nullptr && !m_head.compare_exchange_weak(oldHead, oldHead->next));

    if (oldHead != nullptr)
    {
        dataPointer = std::move(oldHead->data);
        //释放此线程持有的风险指针
        thisThreadHazardPoint->hazardStorePoint.store(nullptr);
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
    //清空待删除队列
    DelteWaitQueue();

    return dataPointer;
}

template <typename T>
inline void LockFreeQueue<T>::push(const T &value)
{
    T tempValue = value;
    push(std::move(tempValue));
}

template <typename T>
inline void LockFreeQueue<T>::push(T &&value)
{
    QueueNode *newNode = new QueueNode();
    newNode->data = std::make_shared<T>(std::move(value));
    QueueNode *oldTail;
    do
    {
        oldTail = m_tail.load();
        oldTail->next = newNode;
    } while (!m_tail.compare_exchange_weak(oldTail, newNode));
    m_size.fetch_add(1);
}

template <typename T>
inline int LockFreeQueue<T>::size()
{
    return m_size.load();
}

template <typename T>
inline void LockFreeQueue<T>::AddToDeleteWaitQueue(QueueNode *waitDeletePointer)
{
    QueueNode *oldWaitDeleteHead;
    do
    {
        oldWaitDeleteHead = m_deleteWaitHead.load();
        waitDeletePointer->next = oldWaitDeleteHead;
    } while (m_deleteWaitHead.compare_exchange_weak(oldWaitDeleteHead, waitDeletePointer));
}

template <typename T>
inline void LockFreeQueue<T>::DelteWaitQueue()
{
    QueueNode *waitDeleteHead = m_deleteWaitHead.exchange(nullptr);
    QueueNode *waitDeleteHeadNext;
    while (waitDeleteHead != nullptr)
    {
        waitDeleteHeadNext = waitDeleteHead->next;
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