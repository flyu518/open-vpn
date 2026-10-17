#ifdef _WIN32

#if defined(OPENVPN_WINDOWS_AGENT_GENERATED)

// The generated OpenVPN3 agent source owns the Windows service entry point.

#elif defined(OPENVPN_WINDOWS_AGENT_SOURCE)

#define OVPNAGENT_NAME openvpn

#include "openvpn/ovpnagent/win/ovpnagent.cpp"

// OpenVPN3 官方 Windows agent 提供 Service 控制入口、命名管道和 Wintun/TAP
// 生命周期；本项目通过 OVPNAGENT_NAME=openvpn 与 OpenVPN Connect 隔离。

#else

#error "Windows agent source generation is required; do not build an empty service"

#endif

#else

int main() {
    // 非 Windows 构建只保留源码检查入口，不启动系统服务。
    return 0;
}

#endif
