#include "vpn_route_policy.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
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

std::string network_to_pattern(const std::string& address, uint8_t prefix) {
    return address + "/" + std::to_string(static_cast<unsigned int>(prefix));
}

std::string trim_domain_text(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    std::string result = value.substr(begin, end - begin);
    while (!result.empty() && result.back() == '.') result.pop_back();
    for (char& character : result) {
        if (static_cast<unsigned char>(character) >= 128) return {};
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

bool valid_domain_text(const std::string& value) {
    if (value.empty() || value.size() > 253 || value.front() == '.' || value.back() == '.')
        return false;
    std::size_t label_start = 0;
    for (std::size_t index = 0; index <= value.size(); ++index) {
        if (index != value.size() && value[index] != '.') continue;
        const std::size_t label_length = index - label_start;
        if (label_length == 0 || label_length > 63) return false;
        if (value[label_start] == '-' || value[index - 1] == '-') return false;
        for (std::size_t label_index = label_start; label_index < index; ++label_index) {
            const unsigned char character = static_cast<unsigned char>(value[label_index]);
            if (!std::isalnum(character) && character != '-' && character != '_') return false;
        }
        label_start = index + 1;
    }
    return true;
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
    return {RouteDecisionCode::InvalidRoute, "路由格式无效", {}};
}

bool preferred_rule_matches(const RoutePolicy& policy, const std::string& route) {
    for (const RouteRule& rule : policy.rules) {
        if (rule.action == RouteRuleAction::Prefer &&
            route_pattern_matches(route, rule.pattern)) {
            return true;
        }
    }
    return false;
}

bool excluded_by_rule(const RoutePolicy& policy, const std::string& route) {
    for (const RouteRule& rule : policy.rules) {
        if (rule.action == RouteRuleAction::Exclude &&
            route_pattern_matches(route, rule.pattern)) {
            return true;
        }
    }
    return false;
}

bool domain_rules_overlap(const RouteRule& left, const RouteRule& right) {
    if (left.type == RouteRuleType::Ip || right.type == RouteRuleType::Ip) return false;
    if (left.type == RouteRuleType::Domain) {
        return domain_pattern_matches(left.pattern, right.pattern, right.type);
    }
    if (right.type == RouteRuleType::Domain) {
        return domain_pattern_matches(right.pattern, left.pattern, left.type);
    }
    return domain_pattern_matches(left.pattern, right.pattern, RouteRuleType::DomainSuffix) ||
           domain_pattern_matches(right.pattern, left.pattern, RouteRuleType::DomainSuffix);
}

const RouteRule* first_preferred_domain_rule(const RoutePolicy& policy,
                                             const RoutePolicy& other) {
    for (const RouteRule& rule : policy.rules) {
        if (rule.action != RouteRuleAction::Prefer || rule.type == RouteRuleType::Ip) continue;
        for (const RouteRule& other_rule : other.rules) {
            if (other_rule.action == RouteRuleAction::Prefer &&
                domain_rules_overlap(rule, other_rule)) {
                return &rule;
            }
        }
    }
    return nullptr;
}

}  // namespace

bool normalize_route_pattern(const std::string& pattern,
                            bool host_pattern,
                            std::string* normalized) {
    if (normalized == nullptr || pattern.empty()) return false;
    if (pattern.find('/') != std::string::npos) {
        ParsedNetwork parsed;
        if (!parse_network(pattern, &parsed)) return false;
        *normalized = pattern;
        return true;
    }
    if (!host_pattern) return false;
    in_addr ipv4{};
    if (inet_pton(AF_INET, pattern.c_str(), &ipv4) == 1) {
        *normalized = network_to_pattern(pattern, 32);
        return true;
    }
    in6_addr ipv6{};
    if (inet_pton(AF_INET6, pattern.c_str(), &ipv6) == 1) {
        *normalized = network_to_pattern(pattern, 128);
        return true;
    }
    return false;
}

bool route_pattern_matches(const std::string& route,
                           const std::string& pattern) {
    ParsedNetwork route_network;
    ParsedNetwork pattern_network;
    std::string normalized;
    if (!parse_network(route, &route_network) ||
        !normalize_route_pattern(pattern, true, &normalized) ||
        !parse_network(normalized, &pattern_network)) return false;
    return networks_overlap(route_network, pattern_network);
}

bool normalize_domain_pattern(const std::string& pattern,
                              RouteRuleType type,
                              std::string* normalized) {
    if (normalized == nullptr ||
        (type != RouteRuleType::Domain && type != RouteRuleType::DomainSuffix)) {
        return false;
    }
    std::string value = pattern;
    if (type == RouteRuleType::DomainSuffix && value.rfind("*.", 0) == 0) {
        value.erase(0, 2);
    } else if (value.find('*') != std::string::npos) {
        return false;
    }
    value = trim_domain_text(value);
    if (!valid_domain_text(value)) return false;
    *normalized = std::move(value);
    return true;
}

bool domain_pattern_matches(const std::string& host,
                            const std::string& pattern,
                            RouteRuleType type) {
    std::string normalized_host;
    std::string normalized_pattern;
    if (!normalize_domain_pattern(host, RouteRuleType::Domain, &normalized_host) ||
        !normalize_domain_pattern(pattern, type, &normalized_pattern)) {
        return false;
    }
    if (type == RouteRuleType::Domain) return normalized_host == normalized_pattern;
    return normalized_host == normalized_pattern ||
           (normalized_host.size() > normalized_pattern.size() &&
            normalized_host.compare(normalized_host.size() - normalized_pattern.size(),
                                    normalized_pattern.size(), normalized_pattern) == 0 &&
            normalized_host[normalized_host.size() - normalized_pattern.size() - 1] == '.');
}

RouteDecision vpn_route_policy_validate(
    const std::vector<RoutePolicy>& active_policies,
    const RoutePolicy& candidate,
    RouteConflictMode mode) {
    if (candidate.mode == RouteMode::Full) {
        if (!active_policies.empty()) {
            return {RouteDecisionCode::FullTunnelExclusive,
                    "全隧道连接具有排他性：候选会话 " + candidate.session_id +
                        " 不能与活动会话同时接管默认路由",
                    {}};
        }
        return {RouteDecisionCode::Allowed, "允许建立连接", {}};
    }

    if (has_full_tunnel(active_policies)) {
        return {RouteDecisionCode::FullTunnelExclusive,
                "全隧道连接具有排他性：候选会话 " + candidate.session_id +
                    " 不能与活动全隧道同时接管默认路由",
                {}};
    }

    for (const RoutePolicy& active : active_policies) {
        const RouteRule* candidate_rule = first_preferred_domain_rule(candidate, active);
        if (candidate_rule == nullptr) continue;
        const RouteRule* active_rule = first_preferred_domain_rule(active, candidate);
        if (active_rule == nullptr) continue;
        RouteConflict conflict;
        conflict.candidate_route = candidate_rule->pattern;
        conflict.active_session_id = active.session_id;
        conflict.active_route = active_rule->pattern;
        return {RouteDecisionCode::RouteConflict,
                "域名规则与活动 VPN 重叠：候选会话 " + candidate.session_id +
                    " 的 " + candidate_rule->pattern + " 与活动会话 " +
                    active.session_id + " 的 " + active_rule->pattern +
                    " 重叠；需要先断开冲突连接后重新连接",
                {conflict}};
    }

    std::vector<std::string> candidate_routes;
    std::vector<ParsedNetwork> candidate_networks;
    candidate_routes.reserve(candidate.routes.size());
    candidate_networks.reserve(candidate.routes.size());
    for (const std::string& route : candidate.routes) {
        if (excluded_by_rule(candidate, route)) continue;
        ParsedNetwork parsed;
        if (!parse_network(route, &parsed)) {
            return invalid_route();
        }
        candidate_routes.push_back(route);
        candidate_networks.push_back(parsed);
    }

    for (const RoutePolicy& active : active_policies) {
        for (const std::string& active_route : active.routes) {
            ParsedNetwork active_network;
            if (!parse_network(active_route, &active_network)) {
                return invalid_route();
            }
            for (std::size_t candidate_index = 0;
                 candidate_index < candidate_networks.size();
                 ++candidate_index) {
                const ParsedNetwork& candidate_network = candidate_networks[candidate_index];
                const std::string& candidate_route = candidate_routes[candidate_index];
                if (networks_overlap(active_network, candidate_network)) {
                    RouteConflict conflict;
                    conflict.candidate_route = candidate_route;
                    conflict.active_session_id = active.session_id;
                    conflict.active_route = active_route;
                    if (preferred_rule_matches(candidate, candidate_route) ||
                        preferred_rule_matches(active, active_route)) {
                        return {RouteDecisionCode::RouteConflict,
                                "路由与活动 VPN 重叠：候选会话 " + candidate.session_id +
                                    " 的 " + candidate_route + " 与活动会话 " +
                                    active.session_id + " 的 " + active_route +
                                    " 重叠；规则优先，需要先断开冲突连接后重新连接",
                                {conflict}};
                    }
                    if (mode == RouteConflictMode::Strict) {
                        RouteDecision decision{
                            RouteDecisionCode::RouteConflict,
                            "路由与活动 VPN 重叠：候选会话 " + candidate.session_id +
                                " 的 " + candidate_route + " 与活动会话 " +
                                active.session_id + " 的 " + active_route + " 重叠",
                            {conflict}};
                        return decision;
                    }
                }
            }
        }
    }

    RouteDecision decision{RouteDecisionCode::Allowed, "允许建立连接", {}};
    if (mode == RouteConflictMode::Compatible) {
        for (const RoutePolicy& active : active_policies) {
            for (const std::string& active_route : active.routes) {
                ParsedNetwork active_network;
                if (!parse_network(active_route, &active_network)) {
                    return invalid_route();
                }
                for (const std::string& candidate_route : candidate_routes) {
                    ParsedNetwork candidate_network;
                    if (!parse_network(candidate_route, &candidate_network)) {
                        return invalid_route();
                    }
                    if (networks_overlap(active_network, candidate_network)) {
                        const auto duplicate = std::find_if(
                            decision.conflicts.begin(), decision.conflicts.end(),
                            [&](const RouteConflict& conflict) {
                                return conflict.candidate_route == candidate_route &&
                                       conflict.active_session_id == active.session_id &&
                                       conflict.active_route == active_route;
                            });
                        if (duplicate == decision.conflicts.end()) {
                            decision.conflicts.push_back(
                                {candidate_route, active.session_id, active_route});
                        }
                    }
                }
            }
        }
        if (!decision.conflicts.empty()) {
            decision.diagnostic = "兼容模式：当前连接将跳过与活动 VPN 重叠的路由";
        }
    }
    return decision;
}

}  // namespace vpn
