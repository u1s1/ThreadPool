#ifndef HAZARD_POINT
#define HAZARD_POINT

#include<atomic>
#include<thread>

struct alignas(64) HazardPoint
{
    std::atomic<std::thread::id> threadID;
    std::atomic<void *> hazardStorePoint[2];
    HazardPoint()
    {
        threadID.store(std::thread::id(), std::memory_order_relaxed);
        hazardStorePoint[0].store(nullptr, std::memory_order_relaxed);
        hazardStorePoint[1].store(nullptr, std::memory_order_relaxed);
    }
};

class HazardPointManager
{
public:
    HazardPointManager(int nSize): m_nSize(nSize)
    {
        //初始化风险数组
        m_pHazardPointsArray = new HazardPoint[m_nSize];
        std::thread::id tempID;
        for (int i = 0; i < m_nSize; i++)
        {
            m_pHazardPointsArray[i].threadID.store(tempID, std::memory_order_release);
            m_pHazardPointsArray[i].hazardStorePoint[0].store(nullptr, std::memory_order_release);
            m_pHazardPointsArray[i].hazardStorePoint[1].store(nullptr, std::memory_order_release);
        }
    }
    ~HazardPointManager()
    {
        delete[] m_pHazardPointsArray;
    }

    HazardPoint* GetHazardPoint()
    {
        //检查当前线程下是否已有分配的元素
        std::thread::id thisID = std::this_thread::get_id();
        for (size_t i = 0; i < m_nSize; i++)
        {
            if (m_pHazardPointsArray[i].threadID.load(std::memory_order_acquire) == thisID)
            {
                return &m_pHazardPointsArray[i];
            }
        }
        //为当前线程分配元素
        std::thread::id emptyID;
        for (size_t i = 0; i < m_nSize; i++)
        {
            std::thread::id expected = emptyID;
            if (m_pHazardPointsArray[i].threadID.compare_exchange_strong(expected, thisID,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_relaxed))
            {
                m_pHazardPointsArray[i].hazardStorePoint[0].store(nullptr, std::memory_order_release);
                m_pHazardPointsArray[i].hazardStorePoint[1].store(nullptr, std::memory_order_release);
                return &m_pHazardPointsArray[i];
            }
        }
        return nullptr;
    }

    void ReleaseHazardPoint()
    {
        std::thread::id thisID = std::this_thread::get_id();
        std::thread::id emptyID;
        for (size_t i = 0; i < m_nSize; ++i)
        {
            if (m_pHazardPointsArray[i].threadID.load(std::memory_order_acquire) == thisID)
            {
                m_pHazardPointsArray[i].hazardStorePoint[0].store(nullptr, std::memory_order_release);
                m_pHazardPointsArray[i].hazardStorePoint[1].store(nullptr, std::memory_order_release);
                m_pHazardPointsArray[i].threadID.store(emptyID, std::memory_order_release);
                return;
            }
        }
    }

    //检查此指针是否被占用
    bool IsConflictPoint(void* hazardStorePoint)
    {
        for (int i = 0; i < m_nSize; i++)
        {
            if (m_pHazardPointsArray[i].hazardStorePoint[0].load(std::memory_order_acquire) == hazardStorePoint ||
                m_pHazardPointsArray[i].hazardStorePoint[1].load(std::memory_order_acquire) == hazardStorePoint)
            {
                return true;
            }
        }
        return false;
    }

private:
    int m_nSize;
    HazardPoint* m_pHazardPointsArray;
};

#endif