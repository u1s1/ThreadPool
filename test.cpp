#include <iostream>
#include "ThreadPool.h"

int func1(int i)
{
    std::cout << i << std::endl;
    return i;
}

class Test
{
private:
int Test1(int i)
{
    std::cout << "Test1: " << i << std::endl;
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

int main()
{
    Test test;
    test.Run();
    return 0;
}