#include "openvpn3_engine.h"
#include "vpn_security.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

std::atomic<bool> stop_requested{false};

void handle_signal(int) {
    stop_requested.store(true);
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取配置文件：" + path);
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string read_secret(const std::string& prompt) {
    std::cout << prompt << std::flush;

#if defined(_WIN32)
    std::string result;
    for (;;) {
        const int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            std::cout << '\n';
            break;
        }
        if (ch == '\b') {
            if (!result.empty()) {
                result.pop_back();
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
#else
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        std::string fallback;
        std::getline(std::cin, fallback);
        return fallback;
    }

    termios hidden = original;
    hidden.c_lflag &= static_cast<unsigned long>(~ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);

    std::string result;
    std::getline(std::cin, result);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    std::cout << '\n';
    return result;
#endif
}

struct SessionState {
    bool connected = false;
    bool finished = false;
    bool failed = false;
};

class EventTracker {
public:
    void onEvent(const std::string& label, const vpn::EngineEvent& event) {
        const std::string safe_info = vpn::redactSecrets(event.info);
        const bool route_warning = safe_info.find("兼容模式") != std::string::npos ||
                                   safe_info.find("冲突") != std::string::npos ||
                                   safe_info.find("重叠") != std::string::npos;
        if (event.name != "LOG" || event.error || event.fatal || route_warning) {
            std::cout << "[" << label << "] " << event.name;
            if (!safe_info.empty()) {
                std::cout << "：" << safe_info;
            }
            std::cout << '\n';
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            SessionState& state = states_[label];
            if (event.name == "CONNECTED") {
                state.connected = true;
            }
            if (event.name == "DISCONNECTED" || event.name == "FATAL_ERROR" ||
                event.error || event.fatal) {
                state.finished = true;
                state.failed = event.name != "DISCONNECTED" || event.error || event.fatal;
            }
        }
        condition_.notify_all();
    }

    bool waitForConnected(const std::string& label, std::chrono::seconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [&] {
            const auto it = states_.find(label);
            return stop_requested.load() ||
                   (it != states_.end() && (it->second.connected || it->second.failed));
        }) && !stop_requested.load() && states_[label].connected;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::map<std::string, SessionState> states_;
};

struct ProfileInput {
    std::string label;
    std::string path;
    std::string content;
    vpn::EngineCredentials credentials;
};

void print_usage(const char* program) {
    std::cout << "用法：" << program << " [--offline] <profile-a.ovpn> [profile-b.ovpn]\n"
              << "说明：先离线解析配置，再在本机隐藏输入凭据并建立连接。\n"
              << "--offline 只解析配置，不询问密码、不连接网络。\n"
              << "按 Ctrl-C 或回车结束测试；不会把密码写入日志或文件。\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    const bool offline_only = argc > 1 && std::string(argv[1]) == "--offline";
    const int first_profile = offline_only ? 2 : 1;
    const int profile_count = argc - first_profile;
    if (profile_count < 1 || profile_count > 2) {
        print_usage(argv[0]);
        return 2;
    }

    std::signal(SIGINT, handle_signal);
#if !defined(_WIN32)
    std::signal(SIGTERM, handle_signal);
#endif

    try {
        std::vector<ProfileInput> profiles;
        for (int index = first_profile; index < argc; ++index) {
            ProfileInput profile;
            profile.label = index == first_profile ? "A" : "B";
            profile.path = argv[index];
            profile.content = read_file(profile.path);
            profiles.push_back(std::move(profile));
        }

        vpn::OpenVPN3Engine engine;
        for (ProfileInput& profile : profiles) {
            const vpn::ProfileEvaluation evaluation = engine.evaluateProfile(profile.content);
            if (!evaluation.ok) {
                std::cerr << "配置 " << profile.label << " 解析失败："
                          << vpn::redactSecrets(evaluation.message) << '\n';
                return 3;
            }
            std::cout << "配置 " << profile.label << " 解析成功："
                      << evaluation.remote_host << ':' << evaluation.remote_port << ' '
                      << evaluation.remote_protocol << '\n';
        }

        if (offline_only) {
            std::cout << "离线解析完成，未询问凭据，也未建立网络连接。\n";
            return 0;
        }

        for (ProfileInput& profile : profiles) {
            profile.credentials.username =
                read_secret("请输入配置 " + profile.label + " 的用户名：");
            profile.credentials.password =
                read_secret("请输入配置 " + profile.label + " 的密码：");
        }

        EventTracker tracker;
        std::vector<std::unique_ptr<vpn::OpenVPN3Session>> sessions;
        sessions.reserve(profiles.size());

        for (ProfileInput& profile : profiles) {
            auto session = engine.createSession(
                "smoke-" + profile.label,
                [&tracker, label = profile.label](const vpn::EngineEvent& event) {
                    tracker.onEvent(label, event);
                });
            if (!session->setProfile(profile.content).ok ||
                !session->setCredentials(profile.credentials) || !session->connect()) {
                std::cerr << "配置 " << profile.label << " 无法启动连接。\n";
                for (auto& active : sessions) {
                    active->stop();
                }
                return 4;
            }
            sessions.push_back(std::move(session));

            if (!tracker.waitForConnected(profile.label, std::chrono::seconds(120))) {
                std::cerr << "配置 " << profile.label << " 在 120 秒内未连接成功。\n";
                for (auto& active : sessions) {
                    active->stop();
                }
                return 5;
            }
        }

        std::cout << "连接测试已建立 " << sessions.size() << " 个会话。\n"
                  << "请检查对应的 utun/Wintun、路由和 DNS；完成后按回车断开。\n";
        std::string ignored;
        std::getline(std::cin, ignored);
        stop_requested.store(true);
        for (auto& session : sessions) {
            session->stop();
        }
        std::cout << "测试连接已清理。\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "测试程序异常：" << vpn::redactSecrets(error.what()) << '\n';
        return 6;
    }
}
