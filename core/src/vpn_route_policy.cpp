#include "vpn_route_policy.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace vpn {
namespace {

struct ParsedNetwork {
    int family = 0;
    std::array<uint8_t, 16> address{};
    uint8_t prefix_length = 0;
};

bool parse_prefix(std::string_view text, uint8_t max_prefix, uint8_t* out) {
    if (text.empty()) {
        return false;
    }

    unsigned value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10U + static_cast<unsigned>(character - '0');
        if (value > max_prefix) {
            return false;
        }
    }

    *out = static_cast<uint8_t>(value);
    return true;
}

bool parse_network(const std::string& text, ParsedNetwork* out) {
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= text.size()) {
        return false;
    }

    const std::string address_text = text.substr(0, slash);
    const std::string_view prefix_text(text.data() + slash + 1,
                                       text.size() - slash - 1);

    in_addr ipv4{};
    if (inet_pton(AF_INET, address_text.c_str(), &ipv4) == 1) {
        uint8_t prefix = 0;
        if (!parse_prefix(prefix_text, 32, &prefix)) {
            return false;
        }
        out->family = AF_INET;
        out->prefix_length = prefix;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&ipv4);
        std::copy(bytes, bytes + 4, out->address.begin());
        return true;
    }

    in6_addr ipv6{};
    if (inet_pton(AF_INET6, address_text.c_str(), &ipv6) == 1) {
        uint8_t prefix = 0;
        if (!parse_prefix(prefix_text, 128, &prefix)) {
            return false;
        }
        out->family = AF_INET6;
        out->prefix_length = prefix;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&ipv6);
        std::copy(bytes, bytes + 16, out->address.begin());
        return true;
    }

    return false;
}

bool networks_overlap(const ParsedNetwork& left, const ParsedNetwork& right) {
    if (left.family != right.family) {
        return false;
    }

    const unsigned compared_bits =
        left.prefix_length < right.prefix_length ? left.prefix_length : right.prefix_length;
    for (unsigned bit = 0; bit < compared_bits; ++bit) {
        const unsigned byte = bit / 8U;
        const uint8_t mask = static_cast<uint8_t>(0x80U >> (bit % 8U));
        if ((left.address[byte] & mask) != (right.address[byte] & mask)) {
            return false;
        }
    }
    return true;
}

bool has_full_tunnel(const std::vector<RoutePolicy>& policies) {
    for (const RoutePolicy& policy : policies) {
        if (policy.mode == RouteMode::Full) {
            return true;
        }
    }
    return false;
}

RouteDecision invalid_route() {
    return {RouteDecisionCode::InvalidRoute, "路由格式无效"};
}

}  // namespace

RouteDecision vpn_route_policy_validate(
    const std::vector<RoutePolicy>& active_policies,
    const RoutePolicy& candidate) {
    if (candidate.mode == RouteMode::Full) {
        if (!active_policies.empty()) {
            return {RouteDecisionCode::FullTunnelExclusive, "全隧道连接具有排他性"};
        }
        return {RouteDecisionCode::Allowed, "允许建立连接"};
    }

    if (has_full_tunnel(active_policies)) {
        return {RouteDecisionCode::FullTunnelExclusive, "全隧道连接具有排他性"};
    }

    std::vector<ParsedNetwork> candidate_networks;
    candidate_networks.reserve(candidate.routes.size());
    for (const std::string& route : candidate.routes) {
        ParsedNetwork parsed;
        if (!parse_network(route, &parsed)) {
            return invalid_route();
        }
        candidate_networks.push_back(parsed);
    }

    for (const RoutePolicy& active : active_policies) {
        for (const std::string& active_route : active.routes) {
            ParsedNetwork active_network;
            if (!parse_network(active_route, &active_network)) {
                return invalid_route();
            }
            for (const ParsedNetwork& candidate_network : candidate_networks) {
                if (networks_overlap(active_network, candidate_network)) {
                    return {RouteDecisionCode::RouteConflict, "路由与活动 VPN 重叠"};
                }
            }
        }
    }

    return {RouteDecisionCode::Allowed, "允许建立连接"};
}

}  // namespace vpn
