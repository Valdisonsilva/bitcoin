#include <thread>

#ifdef WIN32
__declspec(dllexport) int main(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    return std::thread::hardware_concurrency();
}
