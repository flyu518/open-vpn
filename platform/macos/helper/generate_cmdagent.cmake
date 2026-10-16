if(NOT DEFINED INPUT_SOURCE OR NOT DEFINED OUTPUT_SOURCE)
    message(FATAL_ERROR "需要 INPUT_SOURCE 和 OUTPUT_SOURCE")
endif()

file(READ "${INPUT_SOURCE}" SOURCE)

function(replace_required OLD_TEXT NEW_TEXT DESCRIPTION)
    string(FIND "${SOURCE}" "${OLD_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "OpenVPN3 command agent 补丁未找到目标片段：${DESCRIPTION}")
    endif()
    string(REPLACE "${OLD_TEXT}" "${NEW_TEXT}" SOURCE "${SOURCE}")
    set(SOURCE "${SOURCE}" PARENT_SCOPE)
endfunction()

replace_required(
    [=[            jreq["pid"] = Json::Value(getpid());
            jreq["tun"] = pull.to_json(); // convert TunBuilderCapture to JSON]=]
    [=[            session_name = pull.session_name;
            jreq["pid"] = Json::Value(getpid());
            jreq["session_name"] = Json::Value(session_name);
            jreq["tun"] = pull.to_json(); // convert TunBuilderCapture to JSON]=]
    "tun 建立请求会话标识")

replace_required(
    [=[            // Make transaction
            {
                std::unique_ptr<WS::ClientSet::Transaction> t(new WS::ClientSet::Transaction);
                t->req.method = "GET";
                t->req.uri = "/tun-destroy";
                ts->transactions.push_back(std::move(t));
            }]=]
    [=[            // Make transaction
            Json::Value jreq(Json::objectValue);
            jreq["pid"] = Json::Value(getpid());
            jreq["session_name"] = Json::Value(session_name);
            make_transaction("tun-destroy", jreq.toStyledString(), false, ts);]=]
    "tun 销毁请求会话标识")

replace_required(
    [=[        Config::Ptr config;]=]
    [=[        std::string session_name;
        Config::Ptr config;]=]
    "SetupClient 会话状态")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_SOURCE}" "${SOURCE}")
