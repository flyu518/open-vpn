// Force legal short writes at the OS boundary so frame serialization is tested
// deterministically. The production daemon has no testing switches or commands.
#include <sys/socket.h>
#include <mach/mach.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

static ssize_t fragmented_send(int fd, const void* bytes, size_t size, int flags) {
    const ssize_t sent = ::send(fd, bytes, std::min(size, size_t{1024}), flags);
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    return sent;
}

#define send fragmented_send
#define main daemon_entrypoint
#include "../tools/daemon_main.cpp"
#undef main
#undef send

static mach_msg_type_number_t thread_count() {
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) std::abort();
    for (mach_msg_type_number_t i = 0; i < count; ++i)
        mach_port_deallocate(mach_task_self(), threads[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
    return count;
}

int main(int argc, char** argv) {
    const auto initial_threads = thread_count();
    const int result = daemon_entrypoint(argc, argv);
    if (thread_count() != initial_threads) {
        std::cerr << "daemon 已返回，但客户端或引擎线程仍未退出\n";
        return 7;
    }
    return result;
}
