if(NOT DEFINED INPUT_SOURCE OR NOT DEFINED OUTPUT_SOURCE)
    message(FATAL_ERROR "需要 INPUT_SOURCE 和 OUTPUT_SOURCE")
endif()

file(READ "${INPUT_SOURCE}" SOURCE)

function(replace_required OLD_TEXT NEW_TEXT DESCRIPTION)
    string(FIND "${SOURCE}" "${OLD_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "Windows ovpncli 补丁未找到目标片段：${DESCRIPTION}")
    endif()
    string(REPLACE "${OLD_TEXT}" "${NEW_TEXT}" SOURCE "${SOURCE}")
    set(SOURCE "${SOURCE}" PARENT_SCOPE)
endfunction()

replace_required(
    [=[    void set_rg_local(bool rg_local_arg)
    {
        rg_local = rg_local_arg;
    }]=]
    [=[    void set_rg_local(bool rg_local_arg)
    {
        rg_local = rg_local_arg;
    }

    void set_session_name_from_options(const OptionList &options)
    {
        const OptionList::IndexList *session_options = options.get_index_ptr("setenv");
        if (!session_options)
            return;
        for (const unsigned int index : *session_options)
        {
            const Option &option = options[index];
            if (option.get_optional(1, 64) == "VPN_DESKTOP_SESSION_ID")
                session_name = option.get(2, 256);
        }
    }]=]
    "socket protect 会话标识")

replace_required(
    [=[            return rg_local ? true : WinCommandAgent::add_bypass_route(endpoint);]=]
    [=[            return rg_local ? true : WinCommandAgent::add_bypass_route(endpoint, session_name);]=]
    "Windows 旁路路由会话参数")

replace_required(
    [=[        socket_protect.set_parent(parent);
        RedirectGatewayFlags rg_flags{options};]=]
    [=[        socket_protect.set_parent(parent);
        socket_protect.set_session_name_from_options(options);
        RedirectGatewayFlags rg_flags{options};]=]
    "socket protect 初始化")

replace_required(
    [=[    bool rg_local = false; // do not add bypass route if true
};]=]
    [=[    bool rg_local = false; // do not add bypass route if true
    std::string session_name;
};]=]
    "socket protect 会话字段")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_SOURCE}" "${SOURCE}")
