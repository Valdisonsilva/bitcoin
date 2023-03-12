#include <util-threads.h>

#include <thread>

int hw_concurrency()
{
    return std::thread::hardware_concurrency();
}
