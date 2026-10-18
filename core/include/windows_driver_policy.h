#ifndef VPN_WINDOWS_DRIVER_POLICY_H
#define VPN_WINDOWS_DRIVER_POLICY_H

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace vpn::windows_driver_policy {

inline std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline bool profile_uses_tun(std::string_view profile) {
    std::string dev;
    std::string dev_type;
    bool inside_inline_block = false;
    std::istringstream lines{std::string(profile)};
    std::string line;
    while (std::getline(lines, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;
        const auto text = line.substr(first);
        if (text.front() == '<') {
            if (text.size() > 1 && text[1] == '/')
                inside_inline_block = false;
            else if (text.back() == '>')
                inside_inline_block = true;
            continue;
        }
        if (inside_inline_block || text.front() == '#' || text.front() == ';')
            continue;

        std::istringstream tokens(text);
        std::string directive;
        std::string value;
        tokens >> directive >> value;
        directive = lowercase(std::move(directive));
        value = lowercase(std::move(value));
        if (directive == "dev" && !value.empty())
            dev = std::move(value);
        else if (directive == "dev-type" && !value.empty())
            dev_type = std::move(value);
    }

    if (!dev_type.empty())
        return dev_type == "tun";
    if (dev.empty())
        return true; // OpenVPN's default device type is TUN.
    return dev == "tun" || dev.starts_with("tun");
}

inline bool use_wintun(std::string_view profile, std::string_view configured_driver) {
    const std::string driver = lowercase(std::string(configured_driver));
    if (driver == "wintun")
        return true;
    if (driver == "tap-windows6")
        return false;
    return profile_uses_tun(profile);
}

} // namespace vpn::windows_driver_policy

#endif
