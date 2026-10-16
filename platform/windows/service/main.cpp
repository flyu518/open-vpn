#ifdef _WIN32

#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Windows Service registration and Wintun IPC are implemented in the
    // platform packaging task. This entry point deliberately does not start
    // a VPN or mutate routes by itself.
    return 0;
}

#else

int main() {
    return 0;
}

#endif
