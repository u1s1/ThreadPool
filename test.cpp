#include <iostream>
#include <windows.h>
#include <string>
#include "ThreadPool.h"

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

int main()
{
    Test test;
    test.Run();
    Sleep(3000);
    ThreadPool pool;
    for (int i = 0; i < 3; i++)
    {
        pool.PushThread(&Test::Test1, &test, i);
    }
    Sleep(3000);
    return 0;
}