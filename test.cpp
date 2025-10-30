#include <iostream>
#include <windows.h>
#include <string>
#include <chrono>
#include "ThreadPool.h"
#include "ThreadPoolLockFree.h"

int func1(int i)
{
    std::string str = "Func1: " + std::to_string(i) + "\n";
    std::cout << str;
    return i;
}

class Test
{
public:
int Test1(int i)
{
    std::string str = "Test1: " + std::to_string(i) + "\n";
    std::cout << str;
    return i;
}
private:
    ThreadPool pool;
public:
    Test(){}

    void Run()
    {
        for (int i = 0; i < 3; i++)
        {
            pool.PushThread(&Test::Test1, this, i);
        }
        for (int i = 0; i < 3; i++)
        {
            pool.PushThread(func1, i);
        }
    }
};

LockFreeQueue<int> myQueue;

void funcPush(int i)
{
    myQueue.push(i);
}

void funcPop()
{
    std::shared_ptr<int> pN = myQueue.pop();
    if (pN != nullptr)
    {
        std::string str = std::to_string(*pN.get()) + "\n";
        std::cout << str;
    }
    else
    {
        //std::cout << "Empty\n";
    }
}

int main()
{
    ThreadPoolLockFree pool;
    for (size_t i = 0; i < 100; i++)
    {
        pool.PushThread(funcPush, i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    while (myQueue.size()!=0)
    {
        pool.PushThread(funcPop);
    }
    for (size_t i = 100; i < 10000; i++)
    {
        pool.PushThread(funcPush, i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    while (myQueue.size()!=0)
    {
        pool.PushThread(funcPop);
    }

    return 0;
}