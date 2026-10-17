if(NOT DEFINED INPUT_SOURCE OR NOT DEFINED OUTPUT_SOURCE)
    message(FATAL_ERROR "需要 INPUT_SOURCE 和 OUTPUT_SOURCE")
endif()
if(NOT EXISTS "${INPUT_SOURCE}")
    message(FATAL_ERROR "Windows agent 输入源码不存在：${INPUT_SOURCE}")
endif()

file(READ "${INPUT_SOURCE}" SOURCE)

function(replace_required OLD_TEXT NEW_TEXT DESCRIPTION)
    string(FIND "${SOURCE}" "${OLD_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "Windows agent 补丁未找到目标片段：${DESCRIPTION}")
    endif()
    string(REPLACE "${OLD_TEXT}" "${NEW_TEXT}" SOURCE "${SOURCE}")
    set(SOURCE "${SOURCE}" PARENT_SCOPE)
endfunction()

function(replace_between_required START_TEXT END_TEXT NEW_TEXT DESCRIPTION)
    string(FIND "${SOURCE}" "${START_TEXT}" START_AT)
    if(START_AT EQUAL -1)
        message(FATAL_ERROR "Windows agent 补丁未找到起始片段：${DESCRIPTION}")
    endif()
    string(LENGTH "${START_TEXT}" START_LENGTH)
    math(EXPR CONTENT_AT "${START_AT} + ${START_LENGTH}")
    string(SUBSTRING "${SOURCE}" ${CONTENT_AT} -1 REMAINDER)
    string(FIND "${REMAINDER}" "${END_TEXT}" RELATIVE_END_AT)
    if(RELATIVE_END_AT EQUAL -1)
        message(FATAL_ERROR "Windows agent 补丁未找到结束片段：${DESCRIPTION}")
    endif()
    math(EXPR END_AT "${CONTENT_AT} + ${RELATIVE_END_AT}")
    string(SUBSTRING "${SOURCE}" 0 ${START_AT} PREFIX)
    string(SUBSTRING "${SOURCE}" ${END_AT} -1 SUFFIX)
    set(SOURCE "${PREFIX}${NEW_TEXT}${SUFFIX}" PARENT_SCOPE)
endfunction()

replace_required(
    [=[#include <vector>
#include <memory>
#include <utility>]=]
    [=[#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <cstdint>
#include <vector>
#include <memory>
#include <utility>

#include "vpn_route_policy.h"]=]
    "standard includes")

replace_required(
    [=[using namespace openvpn;]=]
    [=[using namespace openvpn;

using SessionKey = std::string;

struct WindowsAgentSessionState
{
    TunWin::Setup::Ptr tun;
    openvpn_io::windows::object_handle client_process;
    openvpn_io::windows::object_handle client_confirm_event;
    CancelableHandle client_destroy_event;
    std::string remote_tap_handle_hex;
    TunWin::RingBuffer::Ptr ring_buffer;
    DWORD vpn_interface_index = DWORD(-1);

    explicit WindowsAgentSessionState(openvpn_io::io_context &io_context)
        : client_process(io_context),
          client_confirm_event(io_context),
          client_destroy_event(io_context)
    {
    }
};

static std::string session_name_without_mode(const std::string &name)
{
    const std::string mode_marker = "|route-mode=";
    const std::string rules_marker = "|route-rules=";
    std::size_t separator = name.find(mode_marker);
    const std::size_t rules_separator = name.find(rules_marker);
    if (separator == std::string::npos ||
        (rules_separator != std::string::npos && rules_separator < separator))
        separator = rules_separator;
    return separator == std::string::npos ? name : name.substr(0, separator);
}

static vpn::RouteConflictMode route_mode_for_name(const std::string &name)
{
    const std::string marker = "|route-mode=";
    const std::size_t separator = name.find(marker);
    if (separator == std::string::npos)
        return vpn::RouteConflictMode::Compatible;
    const std::size_t end = name.find('|', separator + marker.size());
    const std::string mode = name.substr(separator + marker.size(),
        end == std::string::npos ? std::string::npos : end - separator - marker.size());
    return mode == "compatible" ? vpn::RouteConflictMode::Compatible
                                  : vpn::RouteConflictMode::Strict;
}

static std::vector<vpn::RouteRule> route_rules_for_name(const std::string &name)
{
    const std::string marker = "|route-rules=";
    const std::size_t start = name.find(marker);
    if (start == std::string::npos)
        return {};
    const std::size_t end = name.find('|', start + marker.size());
    const std::string encoded = name.substr(start + marker.size(),
        end == std::string::npos ? std::string::npos : end - start - marker.size());
    std::vector<vpn::RouteRule> rules;
    std::size_t offset = 0;
    while (offset < encoded.size() && rules.size() < 32)
    {
        const std::size_t comma = encoded.find(',', offset);
        const std::string item = encoded.substr(offset,
            comma == std::string::npos ? std::string::npos : comma - offset);
        const std::size_t at = item.find('@');
        if (at != std::string::npos)
        {
            const auto action = item.substr(0, at) == "exclude"
                ? vpn::RouteRuleAction::Exclude : vpn::RouteRuleAction::Prefer;
            vpn::RouteRuleType type = vpn::RouteRuleType::Ip;
            std::size_t pattern_start = at + 1;
            const std::size_t type_separator = item.find(':', pattern_start);
            if (type_separator != std::string::npos)
            {
                const std::string type_name = item.substr(
                    pattern_start, type_separator - pattern_start);
                if (type_name == "domain")
                {
                    type = vpn::RouteRuleType::Domain;
                    pattern_start = type_separator + 1;
                }
                else if (type_name == "domainSuffix")
                {
                    type = vpn::RouteRuleType::DomainSuffix;
                    pattern_start = type_separator + 1;
                }
                else if (type_name == "ip")
                    pattern_start = type_separator + 1;
            }
            const std::string pattern = item.substr(pattern_start);
            if (!pattern.empty())
                rules.push_back({action, pattern, type});
        }
        if (comma == std::string::npos)
            break;
        offset = comma + 1;
    }
    return rules;
}

static SessionKey session_key(const DWORD pid, const std::string &name)
{
    return std::to_string(static_cast<unsigned long long>(pid)) + ":" +
           session_name_without_mode(name);
}

static bool belongs_to_pid(const SessionKey &key, const DWORD pid)
{
    const std::string prefix = std::to_string(static_cast<unsigned long long>(pid)) + ":";
    return key.compare(0, prefix.size(), prefix) == 0;
}

static std::string endpoint_key(const SessionKey &key, const std::string &host)
{
    const std::size_t separator = key.find(':');
    return (separator == std::string::npos ? key : key.substr(0, separator)) + ":" + host;
}

static std::string default_session_name(const void *owner)
{
    return "windows-client-" + std::to_string(
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(owner)));
}]=]
    "session identity helpers")

replace_required(
    [=[          client_process(io_context),
          client_confirm_event(io_context),
          client_destroy_event(io_context),
]=]
    [=[]=]
    "per-session handle constructor")

replace_between_required(
    [=[    Win::ScopedHANDLE tun_get_handle(]=]
    [=[    TunWin::Util::TapNameGuidPair get_adapter_state()]=]
    [=[    vpn::RoutePolicy route_policy_for(const SessionKey &key,
                                           const std::string &name,
                                           const TunBuilderCapture &capture) const
    {
        vpn::RoutePolicy policy;
        policy.session_id = key;
        policy.mode = (capture.reroute_gw.ipv4 || capture.reroute_gw.ipv6)
            ? vpn::RouteMode::Full : vpn::RouteMode::Split;
        policy.rules = route_rules_for_name(name);
        for (const auto &route : capture.add_routes)
            policy.routes.push_back(route.address + "/" +
                std::to_string(static_cast<unsigned int>(route.prefix_length)));
        return policy;
    }

    static std::size_t apply_domain_rules(TunBuilderCapture &capture,
                                          const std::vector<vpn::RouteRule> &rules,
                                          std::ostream &os)
    {
        std::size_t changed = 0;
        for (const auto &rule : rules)
        {
            if (rule.type == vpn::RouteRuleType::Ip)
                continue;
            if (rule.action == vpn::RouteRuleAction::Exclude)
            {
                const auto before = capture.dns_options.search_domains.size();
                capture.dns_options.search_domains.erase(
                    std::remove_if(capture.dns_options.search_domains.begin(),
                                   capture.dns_options.search_domains.end(),
                                   [&](const openvpn::DnsDomain &domain) {
                                       return vpn::domain_pattern_matches(
                                           domain.domain, rule.pattern, rule.type);
                                   }),
                    capture.dns_options.search_domains.end());
                changed += before - capture.dns_options.search_domains.size();
            }
            else if (std::find_if(capture.dns_options.search_domains.begin(),
                                  capture.dns_options.search_domains.end(),
                                  [&](const openvpn::DnsDomain &domain) {
                                      return domain.domain == rule.pattern;
                                  }) == capture.dns_options.search_domains.end())
            {
                capture.dns_options.search_domains.emplace_back(rule.pattern);
                ++changed;
            }
        }
        if (changed > 0)
        {
            os << "域名后缀规则：已更新 " << changed << " 条 Windows 分流 DNS 域名\n";
            if (capture.dns_options.servers.empty())
                os << "域名后缀规则未完全生效：当前 VPN 未提供 DNS 服务器\n";
        }
        return changed;
    }

    vpn::RouteDecision validate_route_policy(const SessionKey &key,
                                             const vpn::RoutePolicy &candidate,
                                             const vpn::RouteConflictMode mode) const
    {
        std::vector<vpn::RoutePolicy> active;
        for (const auto &item : route_policies)
            if (item.first != key)
                active.push_back(item.second);
        return vpn::vpn_route_policy_validate(active, candidate, mode);
    }

    Win::ScopedHANDLE tun_get_handle(]=]
    "route policy methods")

replace_between_required(
    [=[    Win::ScopedHANDLE tun_get_handle(]=]
    [=[    TunWin::Util::TapNameGuidPair get_adapter_state()]=]
    [=[    Win::ScopedHANDLE tun_get_handle(const SessionKey &key,
                                  std::ostream &os,
                                  const TunWin::Type tun_type,
                                  const bool allow_local_dns_resolvers)
    {
        auto &state = session_states[key];
        if (!state.tun)
            state.tun.reset(new TunWin::Setup(io_context_, tun_type,
                                              allow_local_dns_resolvers));
        return Win::ScopedHANDLE(state.tun->get_handle(os));
    }

]=]
    "per-session tun handle")

replace_between_required(
    [=[    TunWin::Util::TapNameGuidPair get_adapter_state()]=]
    [=[    Win::ScopedHANDLE establish_tun(]=]
    [=[    TunWin::Util::TapNameGuidPair get_adapter_state(const SessionKey &key)
    {
        const auto it = session_states.find(key);
        if (it == session_states.end() || !it->second.tun)
            return {};
        return it->second.tun->get_adapter_state();
    }

]=]
    "per-session adapter state")

replace_between_required(
    [=[    Win::ScopedHANDLE establish_tun(]=]
    [=[    // return true if we did any work]=]
    [=[    Win::ScopedHANDLE establish_tun(const SessionKey &key,
                                  const TunBuilderCapture &tbc,
                                  const std::wstring &openvpn_app_path,
                                  const DWORD client_process_id,
                                  Stop *stop,
                                  std::ostream &os,
                                  const TunWin::Type tun_type,
                                  const bool allow_local_dns_resolvers,
                                  const TunWin::Util::TapNameGuidPair &tap)
    {
        auto &state = session_states[key];
        if (!state.tun)
            state.tun.reset(new TunWin::Setup(io_context_, tun_type,
                                              allow_local_dns_resolvers));
        if ((tun_type == TunWin::OvpnDco) && tap.index != DWORD(-1))
            state.tun->set_adapter_state(tap);
        state.tun->set_process_id(client_process_id);

        TunBuilderCapture::Ptr effective =
            TunBuilderCapture::from_json(tbc.to_json());
        effective->session_name = session_name_without_mode(tbc.session_name);
        const vpn::RoutePolicy requested = route_policy_for(key, tbc.session_name, *effective);
        apply_domain_rules(*effective, requested.rules, os);
        std::size_t excluded_routes = 0;
        effective->add_routes.erase(
            std::remove_if(effective->add_routes.begin(), effective->add_routes.end(),
                [&](const TunBuilderCapture::Route &route) {
                    const std::string value = route.address + "/" +
                        std::to_string(static_cast<unsigned int>(route.prefix_length));
                    for (const auto &rule : requested.rules)
                        if (rule.action == vpn::RouteRuleAction::Exclude &&
                            vpn::route_pattern_matches(value, rule.pattern))
                        {
                            ++excluded_routes;
                            return true;
                        }
                    return false;
                }),
            effective->add_routes.end());

        const vpn::RoutePolicy filtered = route_policy_for(key, tbc.session_name, *effective);
        const auto decision = validate_route_policy(key, filtered,
                                                    route_mode_for_name(tbc.session_name));
        if (decision.code != vpn::RouteDecisionCode::Allowed)
            throw Exception(decision.diagnostic);
        if (decision.conflicts.size() > 32)
            throw Exception("冲突路由数量过多，需用户断开一个会话后重试");
        std::set<std::string> skipped_routes;
        for (const auto &conflict : decision.conflicts)
            skipped_routes.insert(conflict.candidate_route);
        if (!skipped_routes.empty())
        {
            effective->add_routes.erase(
                std::remove_if(effective->add_routes.begin(), effective->add_routes.end(),
                    [&](const TunBuilderCapture::Route &route) {
                        const std::string value = route.address + "/" +
                            std::to_string(static_cast<unsigned int>(route.prefix_length));
                        return skipped_routes.find(value) != skipped_routes.end();
                    }),
                effective->add_routes.end());
            os << "兼容模式：会话 " << session_name_without_mode(tbc.session_name)
               << " 跳过 " << skipped_routes.size()
               << " 条冲突路由，保留前一个会话的路由所有权\n";
            for (const auto &conflict : decision.conflicts)
                os << "冲突：当前 " << conflict.candidate_route << "；前一个会话 "
                   << conflict.active_session_id << " 使用 " << conflict.active_route << "\n";
        }
        if (excluded_routes > 0)
            os << "路由规则：当前连接排除 " << excluded_routes << " 条服务器路由\n";

        if (effective->remote_address.defined())
            session_bypass_hosts[key] = effective->remote_address.address;
        auto handle = state.tun->establish(*effective, openvpn_app_path, stop, os,
                                            state.ring_buffer);
        state.vpn_interface_index = state.tun->vpn_interface_index();
        route_policies[key] = route_policy_for(key, tbc.session_name, *effective);
        return Win::ScopedHANDLE(handle);
    }

]=]
    "per-session tun establishment")

replace_between_required(
    [=[    // return true if we did any work]=]
    [=[    void destroy_tun_exit()]=]
    [=[    bool destroy_tun(const SessionKey &key, std::ostream &os)
    {
        auto it = session_states.find(key);
        if (it == session_states.end())
            return false;
        auto &state = it->second;
        bool did_work = false;
        try
        {
            if (state.client_process.is_open() && !state.remote_tap_handle_hex.empty())
            {
                const HANDLE remote_tap_handle =
                    BufHex::parse<HANDLE>(state.remote_tap_handle_hex, "remote TAP handle");
                Win::ScopedHANDLE local_tap_handle;
                if (::DuplicateHandle(state.client_process.native_handle(),
                                      remote_tap_handle, GetCurrentProcess(),
                                      local_tap_handle.ref(), 0, FALSE,
                                      DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE))
                    did_work = true;
            }
        }
        catch (const std::exception &error)
        {
            os << "destroy_tun: exception in remote tap handle close: " << error.what() << "\n";
        }
        try
        {
            state.ring_buffer.reset();
            if (state.tun)
            {
                state.tun->destroy(os);
                did_work = true;
            }
        }
        catch (const std::exception &error)
        {
            os << "destroy_tun: exception in tun teardown: " << error.what() << "\n";
        }
        state.tun.reset();
        state.remote_tap_handle_hex.clear();
        state.client_process.close();
        state.client_confirm_event.close();
        state.client_destroy_event.cancel_and_close();

        const auto endpoint_it = session_bypass_hosts.find(key);
        if (endpoint_it != session_bypass_hosts.end())
        {
            const std::string endpoint = endpoint_key(key, endpoint_it->second);
            bool still_used = false;
            for (const auto &item : session_bypass_hosts)
                if (item.first != key && endpoint_key(item.first, item.second) == endpoint)
                    still_used = true;
            if (!still_used)
            {
                const auto route_it = remove_cmds_bypass_hosts.find(endpoint);
                if (route_it != remove_cmds_bypass_hosts.end())
                {
                    route_it->second.execute(os);
                    remove_cmds_bypass_hosts.erase(route_it);
                }
            }
            session_bypass_hosts.erase(endpoint_it);
        }
        route_policies.erase(key);
        session_states.erase(it);
        return did_work;
    }

    void destroy_tuns_for_pid(const DWORD pid, std::ostream &os)
    {
        std::vector<SessionKey> keys;
        for (const auto &item : session_states)
            if (belongs_to_pid(item.first, pid))
                keys.push_back(item.first);
        for (const auto &key : keys)
            destroy_tun(key, os);
    }

    void destroy_all_tuns(std::ostream &os)
    {
        std::vector<SessionKey> keys;
        for (const auto &item : session_states)
            keys.push_back(item.first);
        for (const auto &key : keys)
            destroy_tun(key, os);
    }

    bool adopt_pending_tun(const DWORD pid,
                           const SessionKey &target_key,
                           const TunWin::Util::TapNameGuidPair &tap,
                           std::ostream &os)
    {
        SessionKey pending_key;
        for (const auto &item : session_states)
        {
            if (item.first == target_key || !belongs_to_pid(item.first, pid) || !item.second.tun)
                continue;
            const auto current = item.second.tun->get_adapter_state();
            if (current.index == tap.index && current.guid == tap.guid && current.name == tap.name)
            {
                pending_key = item.first;
                break;
            }
        }
        if (pending_key.empty())
            return false;

        if (session_states.find(target_key) != session_states.end())
            destroy_tun(target_key, os);

        auto node = session_states.extract(pending_key);
        node.key() = target_key;
        session_states.insert(std::move(node));

        auto bypass = session_bypass_hosts.find(pending_key);
        if (bypass != session_bypass_hosts.end())
        {
            session_bypass_hosts[target_key] = std::move(bypass->second);
            session_bypass_hosts.erase(bypass);
        }
        auto policy = route_policies.find(pending_key);
        if (policy != route_policies.end())
        {
            route_policies[target_key] = std::move(policy->second);
            route_policies.erase(policy);
        }

        // The /tun-open connection is only a temporary owner.  Keep its
        // confirm event and remote TAP handle: DCO /tun-setup intentionally
        // does not resend confirm_event, and the client still signals it
        // after taking ownership of the handle.
        os << "采用待确认的 DCO 隧道：" << target_key << "\n";
        return true;
    }

]=]
    "per-session cleanup")

replace_between_required(
    [=[    void destroy_tun_exit()]=]
    [=[    void set_client_process(]=]
    [=[    void destroy_tun_exit()
    {
        std::ostringstream os;
        destroy_all_tuns(os);
        if (!os.str().empty())
            OPENVPN_LOG_NTNL("TUN CLOSE (exit)\n" << os.str());
    }

]=]
    "service cleanup")

replace_between_required(
    [=[    void set_client_process(]=]
    [=[    void set_client_confirm_event(]=]
    [=[    void set_client_process(const SessionKey &key, Win::ScopedHANDLE &&proc)
    {
        auto &state = session_states[key];
        state.client_process.close();
        state.client_process.assign(proc.release());
        state.client_process.async_wait([self = Ptr(this), key](
            const openvpn_io::error_code &error) {
            if (!error)
            {
                std::ostringstream os;
                self->destroy_tun(key, os);
                OPENVPN_LOG_NTNL("TUN CLOSE (failsafe)\n" << os.str());
            }
        });
    }

]=]
    "process cleanup")

replace_between_required(
    [=[    void set_client_confirm_event(]=]
    [=[    void set_client_destroy_event(]=]
    [=[    void set_client_confirm_event(const SessionKey &key,
                                      const std::string &confirm_handle_hex)
    {
        auto &state = session_states[key];
        state.client_confirm_event.close();
        const HANDLE remote_event =
            BufHex::parse<HANDLE>(confirm_handle_hex, "confirm event handle");
        HANDLE event_handle = nullptr;
        if (!::DuplicateHandle(get_client_process(key), remote_event,
                                GetCurrentProcess(), &event_handle, 0, FALSE,
                                DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE))
            OPENVPN_THROW_EXCEPTION("set_client_confirm_event: DuplicateHandle failed");
        state.client_confirm_event.assign(event_handle);
        const DWORD status = ::WaitForSingleObject(
            state.client_confirm_event.native_handle(), 0);
        if (status == WAIT_ABANDONED)
            throw Exception("set_client_confirm_event: confirm event is abandoned");
        state.client_confirm_event.async_wait([self = Ptr(this), key](
            const openvpn_io::error_code &error) {
            if (!error)
            {
                const auto it = self->session_states.find(key);
                if (it != self->session_states.end())
                    it->second.remote_tap_handle_hex.clear();
            }
        });
    }

]=]
    "confirm cleanup")

replace_between_required(
    [=[    void set_client_destroy_event(]=]
    [=[    HANDLE get_client_process(]=]
    [=[    void set_client_destroy_event(const SessionKey &key,
                                      const std::string &event_handle_hex)
    {
        auto &state = session_states[key];
        state.client_destroy_event.cancel_and_close();
        const HANDLE remote_event =
            BufHex::parse<HANDLE>(event_handle_hex, "destroy event handle");
        HANDLE event_handle = nullptr;
        if (!::DuplicateHandle(get_client_process(key), remote_event,
                                GetCurrentProcess(), &event_handle, 0, FALSE,
                                DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE))
            OPENVPN_THROW_EXCEPTION("set_client_destroy_event: DuplicateHandle failed");
        state.client_destroy_event.assign(event_handle);
        state.client_destroy_event.check_is_already_signalled();
        state.client_destroy_event.async_wait([self = Ptr(this), key](
            const openvpn_io::error_code &error) {
            if (!error)
            {
                std::ostringstream os;
                self->destroy_tun(key, os);
                OPENVPN_LOG_NTNL("TUN CLOSE (event)\n" << os.str());
            }
        });
    }

]=]
    "destroy event cleanup")

replace_between_required(
    [=[    HANDLE get_client_process(]=]
    [=[    void set_remote_tap_handle_hex(]=]
    [=[    HANDLE get_client_process(const SessionKey &key)
    {
        const auto it = session_states.find(key);
        if (it == session_states.end() || !it->second.client_process.is_open())
            throw Exception("no client process");
        return it->second.client_process.native_handle();
    }

]=]
    "client process lookup")

replace_required(
    [=[    void set_remote_tap_handle_hex(const HANDLE tap_handle)
    {
        remote_tap_handle_hex = Win::HandleComm::send_handle(tap_handle, get_client_process());
    }]=]
    [=[    void set_remote_tap_handle_hex(const SessionKey &key, const HANDLE tap_handle)
    {
        session_states[key].remote_tap_handle_hex =
            Win::HandleComm::send_handle(tap_handle, get_client_process(key));
    }]=]
    "remote tap handle")

replace_required(
    [=[    const std::string &get_remote_tap_handle_hex()
    {
        return remote_tap_handle_hex;
    }]=]
    [=[    const std::string &get_remote_tap_handle_hex(const SessionKey &key)
    {
        return session_states.at(key).remote_tap_handle_hex;
    }]=]
    "remote tap getter")

replace_required(
    [=[    void assign_ring_buffer(TunWin::RingBuffer *ring_buffer_arg)
    {
        ring_buffer.reset(ring_buffer_arg);
    }]=]
    [=[    void assign_ring_buffer(const SessionKey &key, TunWin::RingBuffer *ring_buffer_arg)
    {
        session_states[key].ring_buffer.reset(ring_buffer_arg);
    }]=]
    "ring buffer")

replace_between_required(
    [=[    void add_bypass_route(]=]
    [=[#ifdef OPENVPN_AGENT_START_PROCESS]=]
    [=[    void add_bypass_route(const SessionKey &key,
                               const std::string &host,
                               const bool ipv6)
    {
        auto &state = session_states[key];
        const std::string endpoint = endpoint_key(key, host);
        std::ostringstream os;
        remove_cmds_bypass_hosts[endpoint].execute(os);
        remove_cmds_bypass_hosts[endpoint].clear();
        ActionList add_cmds;
        const ADDRESS_FAMILY af = ipv6 ? AF_INET6 : AF_INET;
        const TunWin::Util::BestGateway gateway{af, host, state.vpn_interface_index};
        TunWin::Setup::add_bypass_route(
            gateway, host, ipv6, add_cmds, remove_cmds_bypass_hosts[endpoint]);
        add_cmds.execute(os);
        session_bypass_hosts[key] = host;
        OPENVPN_LOG(os.str());
    }

]=]
    "bypass route")

replace_required(
    [=[    ActionList remove_cmds_bypass_hosts;

    TunWin::RingBuffer::Ptr ring_buffer;]=]
    [=[]=]
    "listener global session state")

replace_between_required(
    [=[    TunWin::Setup::Ptr tun;
    openvpn_io::windows::object_handle client_process;
    openvpn_io::windows::object_handle client_confirm_event;
    CancelableHandle client_destroy_event;
    std::string remote_tap_handle_hex;
    openvpn_io::io_context &io_context_;

    // with persist tunnel and redirect-gw we must exclude
    // VPN interface when searching for best gateway when
    // adding bypass route for the next remote
]=]
    [=[};]=]
    [=[    openvpn_io::io_context &io_context_;

    std::map<SessionKey, WindowsAgentSessionState> session_states;
    std::map<SessionKey, std::string> session_bypass_hosts;
    std::map<std::string, ActionList> remove_cmds_bypass_hosts;
    std::map<SessionKey, vpn::RoutePolicy> route_policies;

    WindowsAgentSessionState &session_state(const SessionKey &key)
    {
        return session_states.try_emplace(key, io_context_).first->second;
    }

  public:
    std::string single_session_name(const DWORD pid) const
    {
        std::set<std::string> names;
        const std::string prefix = std::to_string(
            static_cast<unsigned long long>(pid)) + ":";
        const auto collect = [&](const auto &items) {
            for (const auto &item : items)
            {
                if (belongs_to_pid(item.first, pid))
                    names.insert(item.first.substr(prefix.size()));
            }
        };
        collect(session_states);
        collect(session_bypass_hosts);
        collect(route_policies);
        return names.size() == 1 ? *names.begin() : std::string();
    }

  private:
]=]
    "listener session state")

replace_required(
    [=[virtual ~MyClientInstance() = default;

  private:]=]
    [=[virtual ~MyClientInstance() = default;

    const SessionKey &session_key() const
    {
        return session_key_value;
    }

  private:]=]
    "client key accessor")

replace_required(
    [=[    BufferList in;
    BufferPtr out;]=]
    [=[    DWORD session_pid = 0;
    std::string session_name;
    SessionKey session_key_value;
    BufferList in;
    BufferPtr out;]=]
    "client key state")

replace_required([=[parent()->destroy_tun(os)]=]
                 [=[parent()->destroy_tun(session_key(), os)]=]
                 "destroy call")
replace_required([=[parent()->tun_get_handle(os,]=]
                 [=[parent()->tun_get_handle(session_key(), os,]=]
                 "tun open call")
replace_required([=[parent()->set_client_process(]=]
                 [=[parent()->set_client_process(session_key(),]=]
                 "process call")
replace_required([=[parent()->set_client_confirm_event(]=]
                 [=[parent()->set_client_confirm_event(session_key(),]=]
                 "confirm call")
replace_required([=[parent()->set_client_destroy_event(]=]
                 [=[parent()->set_client_destroy_event(session_key(),]=]
                 "destroy event call")
replace_required([=[parent()->assign_ring_buffer(]=]
                 [=[parent()->assign_ring_buffer(session_key(),]=]
                 "ring call")
replace_required([=[parent()->establish_tun(*tbc,]=]
                 [=[parent()->establish_tun(session_key(), *tbc,]=]
                 "establish call")
replace_required([=[parent()->set_remote_tap_handle_hex(]=]
                 [=[parent()->set_remote_tap_handle_hex(session_key(),]=]
                 "remote tap call")
replace_required([=[parent()->get_remote_tap_handle_hex()]=]
                 [=[parent()->get_remote_tap_handle_hex(session_key())]=]
                 "remote tap getter call")
replace_required([=[parent()->get_adapter_state()]=]
                 [=[parent()->get_adapter_state(session_key())]=]
                 "adapter state call")
replace_required([=[parent()->get_client_process()]=]
                 [=[parent()->get_client_process(session_key())]=]
                 "ring buffer process call")
replace_required([=[parent()->add_bypass_route(host, ipv6)]=]
                 [=[parent()->add_bypass_route(session_key(), host, ipv6)]=]
                 "bypass call")

replace_required(
    [=[if (parent()->destroy_tun(session_key(), os))
                    {
                        os << "Destroyed previous TAP instance\n";
                        ::Sleep(1000);
                    }]=]
    [=[session_pid = json::get_uint_optional(root, "pid", 0);
            session_name = root.isMember("session_name")
                ? json::get_string(root, "session_name") : default_session_name(this);
            session_key_value = ::session_key(session_pid, session_name);
            if (parent()->destroy_tun(session_key(), os))
            {
                os << "Destroyed previous TAP instance\n";
                ::Sleep(1000);
            }]=]
    "tun open identity")

replace_required(
    [=[tbc->validate();]=]
    [=[tbc->validate();
            session_pid = pid;
            session_name = tbc->session_name.empty()
                ? default_session_name(this) : tbc->session_name;
            session_key_value = ::session_key(session_pid, session_name);]=]
    "tun setup identity")

replace_required(
    [=[                    if (tun_type != TunWin::OvpnDco && parent()->destroy_tun(session_key(), os))
                    {
                        os << "Destroyed previous TAP instance\n";
                        ::Sleep(1000);
                    }

                    // pre-establish impersonation]=]
    [=[                    if (tun_type != TunWin::OvpnDco && parent()->destroy_tun(session_key(), os))
                    {
                        os << "Destroyed previous TAP instance\n";
                        ::Sleep(1000);
                    }

                    TunWin::Util::TapNameGuidPair pending_tap;
                    if (tun_type == TunWin::OvpnDco)
                    {
                        pending_tap.guid = json::get_string(root, "adapter_guid");
                        pending_tap.index = (DWORD)json::get_int(root, "adapter_index");
                        pending_tap.name = json::get_string(root, "adapter_name");
                        parent()->adopt_pending_tun(session_pid, session_key(), pending_tap, os);
                    }

                    // pre-establish impersonation]=]
    "DCO pending tunnel adoption")

replace_required(
    [=[bool ipv6 = json::get_bool(root, "ipv6");]=]
    [=[if (session_key_value.empty())
        {
            session_pid = json::get_uint_optional(root, "pid", 0);
            session_name = json::get_string_optional(root, "session_name", std::string());
            if (session_name.empty())
                session_name = parent()->single_session_name(session_pid);
            if (session_name.empty())
                throw Exception("/add-bypass-route requires an unambiguous session_name");
            session_key_value = ::session_key(session_pid, session_name);
        }
        bool ipv6 = json::get_bool(root, "ipv6");]=]
    "bypass identity")

string(REPLACE [=[session_states[key]]=]
               [=[session_state(key)]=]
               SOURCE "${SOURCE}")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_SOURCE}" DIRECTORY)
if(OUTPUT_DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
endif()
file(WRITE "${OUTPUT_SOURCE}" "${SOURCE}")
