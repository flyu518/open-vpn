if(NOT DEFINED INPUT_SOURCE OR NOT DEFINED OUTPUT_SOURCE)
    message(FATAL_ERROR "需要 INPUT_SOURCE 和 OUTPUT_SOURCE")
endif()

file(READ "${INPUT_SOURCE}" SOURCE)

function(replace_required OLD_TEXT NEW_TEXT DESCRIPTION)
    string(FIND "${SOURCE}" "${OLD_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "Windows command agent 补丁未找到目标片段：${DESCRIPTION}")
    endif()
    string(REPLACE "${OLD_TEXT}" "${NEW_TEXT}" SOURCE "${SOURCE}")
    set(SOURCE "${SOURCE}" PARENT_SCOPE)
endfunction()

replace_required(
    [=[#include <utility>]=]
    [=[#include <string>
#include <utility>]=]
    "标准库 include")

replace_required(
    [=[namespace openvpn {]=]
    [=[namespace openvpn {

static std::string desktop_session_name(const OptionList &options)
{
    std::string session_name;
    const OptionList::IndexList *session_options = options.get_index_ptr("setenv");
    if (session_options)
    {
        for (const unsigned int index : *session_options)
        {
            const Option &option = options[index];
            if (option.get_optional(1, 64) == "VPN_DESKTOP_SESSION_ID")
                session_name = option.get(2, 256);
        }
    }
    return session_name;
}]=]
    "桌面会话标识读取")

replace_required(
    [=[        jreq["confirm_event"] = confirm_event.duplicate_local();
            jreq["destroy_event"] = destroy_event.duplicate_local();
            jreq["allow_local_dns_resolvers"] = config->allow_local_dns_resolvers;]=]
    [=[        jreq["confirm_event"] = confirm_event.duplicate_local();
            jreq["destroy_event"] = destroy_event.duplicate_local();
            jreq["pid"] = Json::Value(static_cast<unsigned int>(::GetCurrentProcessId()));
            jreq["session_name"] = config->session_name;
            jreq["allow_local_dns_resolvers"] = config->allow_local_dns_resolvers;]=]
    "tun-open 会话标识")

replace_required(
    [=[            os << "SetupClient: transmitting tun setup list to " << config->npserv << "\n";

            // Build JSON request
            Json::Value jreq(Json::objectValue);]=]
    [=[            os << "SetupClient: transmitting tun setup list to " << config->npserv << "\n";
            session_name = pull.session_name;

            // Build JSON request
            Json::Value jreq(Json::objectValue);
            jreq["pid"] = Json::Value(static_cast<unsigned int>(::GetCurrentProcessId()));
            jreq["session_name"] = session_name;]=]
    "tun-setup 会话标识")

replace_required(
    [=[            jreq["destroy_event"] = destroy_event.duplicate_local();
            if (config->tun_type != TunWin::OvpnDco)]=]
    [=[            jreq["destroy_event"] = destroy_event.duplicate_local();
            jreq["session_name"] = session_name;
            if (config->tun_type != TunWin::OvpnDco)]=]
    "tun-setup 会话名")

replace_required(
    [=[    static bool add_bypass_route(IP::Addr endpoint)]=]
    [=[    static bool add_bypass_route(IP::Addr endpoint,
                                      const std::string &session_name)]=]
    "旁路路由会话参数")

replace_required(
    [=[        jreq["host"] = endpoint.to_string();
        jreq["ipv6"] = endpoint.is_ipv6();]=]
    [=[        jreq["pid"] = Json::Value(static_cast<unsigned int>(::GetCurrentProcessId()));
        jreq["session_name"] = session_name;
        jreq["host"] = endpoint.to_string();
        jreq["ipv6"] = endpoint.is_ipv6();]=]
    "旁路路由请求会话标识")

replace_required(
    [=[        std::string npserv = Agent::named_pipe_path();    // server pipe
        std::string client_exe = Win::module_name_utf8(); // for validation
        int debug_level = 1;]=]
    [=[        std::string npserv = Agent::named_pipe_path();    // server pipe
        std::string client_exe = Win::module_name_utf8(); // for validation
        std::string session_name;
        int debug_level = 1;]=]
    "Config 会话标识")

replace_required(
    [=[        SetupClient(openvpn_io::io_context &io_context,
                    const Config::Ptr &config_arg)
            : config(config_arg),]=]
    [=[        SetupClient(openvpn_io::io_context &io_context,
                    const Config::Ptr &config_arg)
            : config(config_arg),]=]
    "SetupClient 构造函数")

replace_required(
    [=[        Config::Ptr config;
        openvpn_io::windows::object_handle service_process;]=]
    [=[        std::string session_name;
        Config::Ptr config;
        openvpn_io::windows::object_handle service_process;]=]
    "SetupClient 会话状态")

replace_required(
    [=[    WinCommandAgent(const OptionList &opt_parent)
    {
        config.reset(new Config);
    }]=]
    [=[    WinCommandAgent(const OptionList &opt_parent)
    {
        config.reset(new Config);
        config->session_name = desktop_session_name(opt_parent);
    }]=]
    "WinCommandAgent 会话标识")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_SOURCE}" "${SOURCE}")
