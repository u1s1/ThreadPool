#ifndef HAZARD_POINT
#define HAZARD_POINT

#include<atomic>
#include<thread>

struct HazardPoint
{
    std::atomic<std::thread::id> threadID;
    std::atomic<void *> hazardStorePoint[2];
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
            m_pHazardPointsArray[i].threadID.store(tempID);
            m_pHazardPointsArray[i].hazardStorePoint[0].store(nullptr);
            m_pHazardPointsArray[i].hazardStorePoint[1].store(nullptr);
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
            if (m_pHazardPointsArray[i].threadID.load() == thisID)
            {
                return &m_pHazardPointsArray[i];
            }
        }
        //为当前线程分配元素
        std::thread::id emptyID;
        for (size_t i = 0; i < m_nSize; i++)
        {
            std::thread::id expected = emptyID;
            if (m_pHazardPointsArray[i].threadID.compare_exchange_weak(expected, thisID))
            {
                m_pHazardPointsArray[i].hazardStorePoint[0].store(nullptr);
                m_pHazardPointsArray[i].hazardStorePoint[1].store(nullptr);
                return &m_pHazardPointsArray[i];
            }
        }
        return nullptr;
    }

    //检查此指针是否被占用
    bool IsConflictPoint(void* hazardStorePoint)
    {
        for (int i = 0; i < m_nSize; i++)
        {
            if (m_pHazardPointsArray[i].hazardStorePoint[0].load() == hazardStorePoint ||
                    m_pHazardPointsArray[i].hazardStorePoint[1].load() == hazardStorePoint)
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