if(NOT DEFINED INPUT_SOURCE OR NOT DEFINED OUTPUT_SOURCE)
    message(FATAL_ERROR "需要 INPUT_SOURCE 和 OUTPUT_SOURCE")
endif()

file(READ "${INPUT_SOURCE}" SOURCE)

function(replace_required OLD_TEXT NEW_TEXT DESCRIPTION)
    string(FIND "${SOURCE}" "${OLD_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "OpenVPN3 agent 补丁未找到目标片段：${DESCRIPTION}")
    endif()
    string(REPLACE "${OLD_TEXT}" "${NEW_TEXT}" SOURCE "${SOURCE}")
    set(SOURCE "${SOURCE}" PARENT_SCOPE)
endfunction()

replace_required(
    [=[#include <iostream>
#include <string>
#include <utility>]=]
    [=[#include <iostream>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>]=]
    "标准库 include")

replace_required(
    [=[        : listen_list(build_listen_list(unix_sock)),
          user_group(user, group, true),
          stats(new MySessionStats),
          event_loop_bar(1)]=]
    [=[        : listen_list(build_listen_list(unix_sock)),
          user_group(user, group, true),
          socket_path(unix_sock ? unix_sock : ""),
          stats(new MySessionStats),
          event_loop_bar(1)]=]
    "helper socket 路径记录")

replace_required(
    [=[    void show_unused_options() const
    {
    }]=]
    [=[    void fix_socket_group() const
    {
        if (socket_path.empty() || user_group.group().empty())
            return;
        struct group *group_info = ::getgrnam(user_group.group().c_str());
        if (group_info == nullptr || ::chown(socket_path.c_str(), static_cast<uid_t>(-1), group_info->gr_gid) != 0)
            OPENVPN_LOG("无法设置 Unix socket 所属组：" << user_group.group());
    }

    void show_unused_options() const
    {
    }]=]
    "helper socket 所属组设置")

replace_required(
    [=[    const Listen::List listen_list;
    const SetUserGroup user_group;
    MySessionStats::Ptr stats;]=]
    [=[    const Listen::List listen_list;
    const SetUserGroup user_group;
    const std::string socket_path;
    MySessionStats::Ptr stats;]=]
    "helper socket 状态字段")

replace_required(
    [=[self->parent->destroy_tun(os);]=]
    [=[self->parent->destroy_tuns_for_pid(pid, os);]=]
    "watchdog 清理调用")

replace_required(
    [=[    ScopedFD establish_tun(const TunBuilderCapture &tbc,
                           TunBuilderSetup::Config *config,
                           Stop *stop,
                           std::ostream &os)
    {
        if (!tun)
            tun.reset(new TunMac::Setup);
        return ScopedFD(tun->establish(tbc, config, stop, os));
    }

    void destroy_tun(std::ostream &os)
    {
        if (tun)
        {
            tun->destroy(os);
            tun.reset();
        }

        remove_cmds_bypass_hosts.execute(os);
        remove_cmds_bypass_hosts.clear();
        bypass_host.clear();
    }

    void set_watchdog(pid_t pid)
    {
        watchdog->watch(pid);
    }

    void unset_watchdog()
    {
        watchdog->unwatch();
    }

    void add_bypass_route(const std::string &host, bool ipv6)
    {
        if (host != bypass_host)
        {
            bypass_host = host;

            std::ostringstream os;

            remove_cmds_bypass_hosts.execute(os);
            remove_cmds_bypass_hosts.clear();

            ActionList add_cmds;
            TunMac::Setup::add_bypass_route(host, ipv6, add_cmds, remove_cmds_bypass_hosts);
            add_cmds.execute(os);

            OPENVPN_LOG(os.str());
        }
    }]=]
    [=[    static std::string session_name_without_mode(const std::string &name)
    {
        const std::string marker = "|route-mode=";
        const std::size_t separator = name.find(marker);
        return separator == std::string::npos ? name : name.substr(0, separator);
    }

    static vpn::RouteConflictMode route_mode_for_name(const std::string &name)
    {
        const std::string marker = "|route-mode=";
        const std::size_t separator = name.find(marker);
        if (separator == std::string::npos)
            return vpn::RouteConflictMode::Compatible;
        const std::string mode = name.substr(separator + marker.size());
        return mode == "compatible" ? vpn::RouteConflictMode::Compatible
                                      : vpn::RouteConflictMode::Strict;
    }

    static std::string session_key(const pid_t pid, const std::string &name)
    {
        return std::to_string(static_cast<long long>(pid)) + ":" +
               session_name_without_mode(name);
    }

    static std::string endpoint_key(const pid_t pid, const std::string &host)
    {
        return std::to_string(static_cast<long long>(pid)) + ":" + host;
    }

    static bool belongs_to_pid(const std::string &key, const pid_t pid)
    {
        const std::string prefix = std::to_string(static_cast<long long>(pid)) + ":";
        return key.compare(0, prefix.size(), prefix) == 0;
    }

    bool has_pid_sessions(const pid_t pid) const
    {
        for (const auto &item : tuns)
        {
            if (belongs_to_pid(item.first, pid))
                return true;
        }
        for (const auto &item : session_bypass_hosts)
        {
            if (belongs_to_pid(item.first, pid))
                return true;
        }
        for (const auto &item : route_policies)
        {
            if (belongs_to_pid(item.first, pid))
                return true;
        }
        return false;
    }

    static vpn::RoutePolicy route_policy(const std::string &key,
                                         const TunBuilderCapture &tbc)
    {
        vpn::RoutePolicy policy;
        policy.session_id = key;
        policy.mode = (tbc.reroute_gw.ipv4 || tbc.reroute_gw.ipv6)
                          ? vpn::RouteMode::Full
                          : vpn::RouteMode::Split;
        for (const auto &route : tbc.add_routes)
        {
            policy.routes.push_back(
                route.address + "/" + std::to_string(static_cast<unsigned int>(route.prefix_length)));
        }
        return policy;
    }

    vpn::RouteDecision validate_route_policy(const std::string &key,
                                             const vpn::RoutePolicy &candidate,
                                             vpn::RouteConflictMode mode) const
    {
        std::vector<vpn::RoutePolicy> active;
        for (const auto &item : route_policies)
        {
            if (item.first != key)
                active.push_back(item.second);
        }
        return vpn::vpn_route_policy_validate(active, candidate, mode);
    }

    ScopedFD establish_tun(const pid_t pid,
                           const std::string &name,
                           const TunBuilderCapture &tbc,
                           TunBuilderSetup::Config *config,
                           Stop *stop,
                           std::ostream &os)
    {
        const std::string base_name = session_name_without_mode(name);
        const std::string key = session_key(pid, name);
        const vpn::RoutePolicy requested = route_policy(key, tbc);
        const vpn::RouteDecision decision =
            validate_route_policy(key, requested, route_mode_for_name(name));
        if (decision.code != vpn::RouteDecisionCode::Allowed)
            throw Exception(decision.diagnostic);
        if (decision.conflicts.size() > 32)
            throw Exception("冲突路由数量过多，需用户断开一个会话后重试");

        TunBuilderCapture::Ptr effective = TunBuilderCapture::from_json(tbc.to_json());
        effective->session_name = base_name;
        std::set<std::string> skipped_routes;
        for (const vpn::RouteConflict &conflict : decision.conflicts)
            skipped_routes.insert(conflict.candidate_route);
        if (!skipped_routes.empty())
        {
            effective->add_routes.erase(
                std::remove_if(effective->add_routes.begin(), effective->add_routes.end(),
                               [&](const TunBuilderCapture::Route &route) {
                                   const std::string value =
                                       route.address + "/" +
                                       std::to_string(static_cast<unsigned int>(route.prefix_length));
                                   return skipped_routes.find(value) != skipped_routes.end();
                               }),
                effective->add_routes.end());
            os << "兼容模式：会话 " << base_name << " 跳过 " << skipped_routes.size()
               << " 条冲突路由，保留前一个会话的路由所有权\n";
            for (const vpn::RouteConflict &conflict : decision.conflicts)
            {
                os << "冲突：当前 " << conflict.candidate_route << "；前一个会话 "
                   << conflict.active_session_id << " 使用 " << conflict.active_route << "\n";
            }
        }

        const vpn::RoutePolicy installed = route_policy(key, *effective);
        TunMac::Setup::Ptr &tun = tuns[key];
        if (!tun)
            tun.reset(new TunMac::Setup);
        if (effective->remote_address.defined())
            session_bypass_hosts[key] = effective->remote_address.address;
        ScopedFD result(tun->establish(*effective, config, stop, os));
        route_policies[key] = installed;
        return result;
    }

    void destroy_tun(const pid_t pid,
                     const std::string &name,
                     std::ostream &os,
                     const bool from_watchdog = false)
    {
        const std::string key = session_key(pid, name);
        const auto tun_it = tuns.find(key);
        if (tun_it != tuns.end())
        {
            if (tun_it->second)
                tun_it->second->destroy(os);
            tuns.erase(tun_it);
        }

        const auto endpoint_it = session_bypass_hosts.find(key);
        if (endpoint_it != session_bypass_hosts.end())
        {
            const auto route_it = remove_cmds_bypass_hosts.find(endpoint_key(pid, endpoint_it->second));
            if (route_it != remove_cmds_bypass_hosts.end())
            {
                route_it->second.execute(os);
                remove_cmds_bypass_hosts.erase(route_it);
            }
            session_bypass_hosts.erase(endpoint_it);
        }
        route_policies.erase(key);

        if (!from_watchdog && !has_pid_sessions(pid))
        {
            const auto watcher_it = watchdogs.find(pid);
            if (watcher_it != watchdogs.end())
            {
                watcher_it->second->unwatch();
                watchdogs.erase(watcher_it);
            }
        }
    }

    void destroy_tuns_for_pid(const pid_t pid, std::ostream &os)
    {
        std::set<std::string> names;
        const std::string prefix = std::to_string(static_cast<long long>(pid)) + ":";
        for (const auto &item : tuns)
        {
            if (belongs_to_pid(item.first, pid))
                names.insert(item.first.substr(prefix.size()));
        }
        for (const auto &item : session_bypass_hosts)
        {
            if (belongs_to_pid(item.first, pid))
                names.insert(item.first.substr(prefix.size()));
        }
        for (const auto &item : route_policies)
        {
            if (belongs_to_pid(item.first, pid))
                names.insert(item.first.substr(prefix.size()));
        }
        for (const std::string &name : names)
            destroy_tun(pid, name, os, true);

        for (auto it = remove_cmds_bypass_hosts.begin(); it != remove_cmds_bypass_hosts.end();)
        {
            if (it->first.compare(0, prefix.size(), prefix) == 0)
            {
                it->second.execute(os);
                it = remove_cmds_bypass_hosts.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void destroy_all_tuns(std::ostream &os)
    {
        std::set<std::string> session_ids;
        for (const auto &item : tuns)
            session_ids.insert(item.first);
        for (const auto &item : session_bypass_hosts)
            session_ids.insert(item.first);
        for (const auto &item : route_policies)
            session_ids.insert(item.first);
        for (const std::string &key : session_ids)
        {
            const std::size_t separator = key.find(':');
            if (separator == std::string::npos)
                continue;
            const pid_t pid = static_cast<pid_t>(std::stoll(key.substr(0, separator)));
            destroy_tun(pid, key.substr(separator + 1), os);
        }
    }

    void set_watchdog(pid_t pid)
    {
        if (pid == -1)
            return;
        auto watcher_it = watchdogs.find(pid);
        if (watcher_it == watchdogs.end())
        {
            WatchdogThread::Ptr watcher(new WatchdogThread(this, io_context));
            watcher->watch(pid);
            watchdogs.emplace(pid, std::move(watcher));
        }
        else
        {
            watcher_it->second->watch(pid);
        }
    }

    void unset_watchdog()
    {
        for (auto &item : watchdogs)
            item.second->unwatch();
        watchdogs.clear();
    }

    void add_bypass_route(const pid_t pid, const std::string &host, bool ipv6)
    {
        if (pid == -1)
            return;
        const std::string key = endpoint_key(pid, host);
        if (host != bypass_hosts[key])
        {
            bypass_hosts[key] = host;

            std::ostringstream os;
            remove_cmds_bypass_hosts[key].execute(os);
            remove_cmds_bypass_hosts[key].clear();

            ActionList add_cmds;
            TunMac::Setup::add_bypass_route(host, ipv6, add_cmds, remove_cmds_bypass_hosts[key]);
            add_cmds.execute(os);

            OPENVPN_LOG(os.str());
        }
    }]=]
    "会话状态按 PID 和会话名分组")

replace_required(
    [=[        : WS::Server::Listener(io_context, config, listen_list, client_factory)
    {
        watchdog.reset(new WatchdogThread(this, io_context));
    }]=]
    [=[        : WS::Server::Listener(io_context, config, listen_list, client_factory),
          io_context(io_context)
    {
    }]=]
    "listener 构造函数")

replace_required(
    [=[    std::string bypass_host;
    ActionList remove_cmds_bypass_hosts;

    TunMac::Setup::Ptr tun;
    WatchdogThread::Ptr watchdog;]=]
    [=[    openvpn_io::io_context &io_context;
    std::map<std::string, std::string> bypass_hosts;
    std::map<std::string, ActionList> remove_cmds_bypass_hosts;
    std::map<std::string, std::string> session_bypass_hosts;
    std::map<std::string, TunMac::Setup::Ptr> tuns;
    std::map<std::string, vpn::RoutePolicy> route_policies;
    std::map<pid_t, WatchdogThread::Ptr> watchdogs;]=]
    "listener 会话状态")

replace_required(
    [=[                    tbc->validate();

                    // get config]=]
    [=[                    tbc->validate();
                    session_name = tbc->session_name;

                    // get config]=]
    "tun 会话名记录")

replace_required(
    [=[                    send_fd = parent()->establish_tun(*tbc, &config, nullptr, os);]=]
    [=[                    send_fd = parent()->establish_tun(pid, session_name, *tbc, &config, nullptr, os);]=]
    "tun 建立调用")

replace_required(
    [=[                    parent()->add_bypass_route(host, ipv6);]=]
    [=[                    parent()->add_bypass_route(pid, host, ipv6);]=]
    "旁路路由调用")

replace_required(
    [=[                parent()->destroy_tun(os);]=]
    [=[                parent()->destroy_tun(session_pid, session_name, os);]=]
    "客户端销毁调用")

replace_required(
    [=[            parent()->destroy_tun(os);]=]
    [=[            parent()->destroy_tun(session_pid, session_name, os);]=]
    "客户端异常清理调用")

replace_required(
    [=[                    generate_reply(jout);
                }
            }
            else if (req.method == "GET" && req.uri == "/tun-destroy")]=]
    [=[                    generate_reply(jout);
                }
                else if (req.uri == "/tun-destroy")
                {
                    pid_t pid = json::get_int_optional(root, "pid", -1);
                    session_pid = pid;
                    session_name = json::get_string_optional(root, "session_name", std::string());
                    parent()->destroy_tun(pid, session_name, os);

                    Json::Value jout(Json::objectValue);
                    jout["log_txt"] = Json::Value(string::remove_blanks(os.str()));
                    generate_reply(jout);
                }
            }
            else if (req.method == "GET" && req.uri == "/tun-destroy")]=]
    "POST tun 销毁请求")

replace_required(
    [=[                    pid_t pid = json::get_int_optional(root, "pid", -1);
                    if (pid != -1)]=]
    [=[                    pid_t pid = json::get_int_optional(root, "pid", -1);
                    session_pid = pid;
                    if (pid != -1)]=]
    "客户端 PID 记录")

replace_required(
    [=[    ScopedFD send_fd;
    BufferList in;]=]
    [=[    pid_t session_pid = -1;
    std::string session_name;
    ScopedFD send_fd;
    BufferList in;]=]
    "客户端会话字段")

replace_required(
    [=[            listener->stop();
            listener->unset_watchdog();]=]
    [=[            listener->stop();
            std::ostringstream os;
            listener->destroy_all_tuns(os);
            OPENVPN_LOG(os.str());
            listener->unset_watchdog();]=]
    "helper 退出清理")

replace_required(
    [=[        config->unix_mode = 0777;]=]
    [=[        config->unix_mode = 0660;]=]
    "Unix socket 权限")

replace_required(
    [=[        serv->start();

        // barrier prior to event-loop entry]=]
    [=[        serv->start();
        tc.fix_socket_group();

        // barrier prior to event-loop entry]=]
    "helper 启动阶段")

replace_required(
    [=[    bool allow_client(AsioPolySock::Base &sock) override
    {
        return true;
    }]=]
    [=[    bool allow_client(AsioPolySock::Base &sock) override
    {
        SockOpt::Creds credentials;
        if (!sock.peercreds(credentials))
        {
            OPENVPN_LOG("拒绝没有 peer credentials 的 Unix socket 客户端");
            return false;
        }
        const bool same_group = credentials.gid == ::getegid();
        const bool non_root = !credentials.root_uid();
        if (!same_group || !non_root)
        {
            OPENVPN_LOG("拒绝 Unix socket 客户端：uid/gid 不符合本地 helper 策略");
            return false;
        }
        return true;
    }]=]
    "Unix socket peer credentials 校验")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_SOURCE}" "${SOURCE}")
