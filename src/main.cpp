#include <iostream>

#ifdef __linux__
namespace mdviewer::linux_platform {
    int RunLinuxApp(int argc, char* argv[]);
}
#endif

int main(int argc, char* argv[]) {
#ifdef __linux__
    return mdviewer::linux_platform::RunLinuxApp(argc, argv);
#else
    (void)argc;
    (void)argv;
    // For now, just a placeholder. 
    // On Windows with WIN32 subsystem, WinMain is the entry point.
    std::cout << "Starting MD Viewer..." << std::endl;
    return 0;
#endif
}
