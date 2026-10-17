if(NOT DEFINED INPUT_SOURCE OR NOT DEFINED OUTPUT_SOURCE)
    message(FATAL_ERROR "需要 INPUT_SOURCE 和 OUTPUT_SOURCE")
endif()

file(READ "${INPUT_SOURCE}" SOURCE)

function(replace_required OLD_TEXT NEW_TEXT DESCRIPTION)
    string(FIND "${SOURCE}" "${OLD_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "Windows cliopt 补丁未找到目标片段：${DESCRIPTION}")
    endif()
    string(REPLACE "${OLD_TEXT}" "${NEW_TEXT}" SOURCE "${SOURCE}")
    set(SOURCE "${SOURCE}" PARENT_SCOPE)
endfunction()

replace_required(
    [=[        return remote_list->current_server_host();]=]
    [=[        if (options)
        {
            const OptionList::IndexList *session_options = options->get_index_ptr("setenv");
            if (session_options)
            {
                for (const unsigned int index : *session_options)
                {
                    const Option &option = (*options)[index];
                    const std::string key = option.get_optional(1, 64);
                    if (key == "VPN_DESKTOP_SESSION_ID")
                        session_name_override = option.get(2, 256);
                    else if (key == "VPN_DESKTOP_ROUTE_MODE")
                        route_mode_override = option.get(2, 64);
                    else if (key == "VPN_DESKTOP_ROUTE_RULES")
                        route_rules_override = option.get(2, 1024);
                }
            }
        }
        const std::string base_name = session_name_override.empty()
                                          ? remote_list->current_server_host()
                                          : session_name_override;
        if (!route_mode_override.empty())
            session_name_override = base_name + "|route-mode=" + route_mode_override;
        if (!route_rules_override.empty())
            session_name_override += "|route-rules=" + route_rules_override;
        if (!session_name_override.empty())
            return session_name_override;
        return base_name;]=]
    "桌面会话标识")

replace_required(
    [=[    std::string load_transport_config()]=]
    [=[    std::string load_transport_config(const OptionList *options = nullptr)]=]
    "transport 配置参数")

replace_required(
    [=[        const std::string session_name = load_transport_config();]=]
    [=[        const std::string session_name = load_transport_config(&opt);]=]
    "初次加载桌面会话标识")

replace_required(
    [=[    std::string userlocked_username;]=]
    [=[    std::string session_name_override;
    std::string route_mode_override;
    std::string route_rules_override;
    std::string userlocked_username;]=]
    "桌面会话标识存储")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_SOURCE}" "${SOURCE}")
