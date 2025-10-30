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
        std::atomic<QueueNode *> next;
        QueueNode() : data(nullptr){
            next.store(nullptr);
        }
    };

public:
    LockFreeQueue(int nHazardPointSize = 16);
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
    m_hazardPointManager = std::make_shared<HazardPointManager>(nHazardPointSize * 2);
}

template <typename T>
inline  LockFreeQueue<T>::~LockFreeQueue()
{
    while (pop() != nullptr);
    delete m_head.load();
}

template <typename T>
inline std::shared_ptr<T> LockFreeQueue<T>::pop()
{
    //获取当前线程下的风险指针
    HazardPoint *thisThreadHazardPoint = m_hazardPointManager->GetHazardPoint();
    if (thisThreadHazardPoint == nullptr)
    {
        return nullptr;
    }
    //需要保护的当前头节点，此节点内已无可用数据，pop后被删除
    QueueNode *oldHead = m_head.load();
    //需要保护的当前头节点，此节点内已无可用数据，用于放入风险数组
    QueueNode *tempNode;
    //头节点的下一个节点，存放着真正需要返回的数据，pop后此节点被置为新的头节点
    QueueNode *returnNode;
    std::shared_ptr<T> dataPointer = nullptr;

    //外层do-while用于更新队列头
    do
    {
        //内层do-while用于更新风险指针
        do
        {
            //更新风险指针
            thisThreadHazardPoint->hazardStorePoint[0].store((void *)m_head.load());
            //更新头节点数据到此处
            tempNode = (QueueNode *)thisThreadHazardPoint->hazardStorePoint[0].load();
            returnNode = tempNode->next.load();
            thisThreadHazardPoint->hazardStorePoint[1].store((void *)returnNode);
            //获取最新头位置
            oldHead = m_head.load();
        //比较最新头位置和风险数组内的是不是同一个
        } while (tempNode != oldHead);
    //若要获取数据的真正节点是空则跳出，或者成功更新头节点到要获取数据的真正节点也跳出
    } while (returnNode != nullptr && !m_head.compare_exchange_weak(oldHead, returnNode));

    if (returnNode != nullptr)
    {
        dataPointer = std::move(returnNode->data);
        //释放此线程持有的风险指针
        thisThreadHazardPoint->hazardStorePoint[0].store(nullptr);
        thisThreadHazardPoint->hazardStorePoint[1].store(nullptr);
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
        thisThreadHazardPoint->hazardStorePoint[0].store(nullptr);
        thisThreadHazardPoint->hazardStorePoint[1].store(nullptr);
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
    newNode->next.store(nullptr);
    QueueNode *oldTail;
    QueueNode *oldTailNext;
    while(true)
    {
        oldTail = m_tail.load();
        oldTailNext = oldTail->next.load();
        if (oldTailNext != nullptr)
        {
            //帮助其他线程推进尾指针
            m_tail.compare_exchange_weak(oldTail, oldTailNext);
            continue;
        }
        //更新next指针为新节点
        if(oldTail->next.compare_exchange_weak(oldTailNext, newNode))
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
    return m_size.load();
}

template <typename T>
inline void LockFreeQueue<T>::AddToDeleteWaitQueue(QueueNode *waitDeletePointer)
{
    QueueNode *oldWaitDeleteHead;
    do
    {
        oldWaitDeleteHead = m_deleteWaitHead.load();
        waitDeletePointer->next.store(oldWaitDeleteHead);
    } while (!m_deleteWaitHead.compare_exchange_weak(oldWaitDeleteHead, waitDeletePointer));
}

template <typename T>
inline void LockFreeQueue<T>::DelteWaitQueue()
{
    QueueNode *waitDeleteHead = m_deleteWaitHead.exchange(nullptr);
    QueueNode *waitDeleteHeadNext;
    while (waitDeleteHead != nullptr)
    {
        waitDeleteHeadNext = waitDeleteHead->next.load();
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