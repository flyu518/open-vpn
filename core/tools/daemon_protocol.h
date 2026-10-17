#pragma once

#include <json/json.h>

#include <memory>
#include <string>
#include <utility>

namespace vpn::daemon {

inline Json::Value response(bool ok, std::string message = {}) {
    Json::Value result(Json::objectValue);
    result["ok"] = ok;
    if (!message.empty()) result["message"] = std::move(message);
    return result;
}

inline std::string write_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value) + "\n";
}

// Validate all consumed fields before conversions, credential access or engine
// creation. JsonCpp's get()/asString()/asBool() can throw on valid JSON types.
inline bool valid_request(const Json::Value& request) {
    if (!request.isObject() || !request["command"].isString()) return false;
    for (const char* key : {"sessionId", "profile", "name", "profileId"}) {
        if (request.isMember(key) && !request[key].isString()) return false;
    }
    if (request.isMember("rememberCredentials") && !request["rememberCredentials"].isBool())
        return false;
    if (request.isMember("credentials")) {
        const auto& credentials = request["credentials"];
        if (!credentials.isObject()) return false;
        for (const char* key : {"username", "password", "response", "dynamicChallengeCookie"}) {
            if (credentials.isMember(key) && !credentials[key].isString()) return false;
        }
    }
    if (request.isMember("routeStrategy")) {
        const auto& strategy = request["routeStrategy"];
        if (!strategy.isObject()) return false;
        if (strategy.isMember("conflictMode")) {
            if (!strategy["conflictMode"].isString()) return false;
            const auto mode = strategy["conflictMode"].asString();
            if (mode != "strict" && mode != "ask" && mode != "compatible") return false;
        }
        if (strategy.isMember("rules")) {
            const auto& rules = strategy["rules"];
            if (!rules.isArray() || rules.size() > 32) return false;
            for (const auto& rule : rules) {
                if (!rule.isObject() || !rule["type"].isString() ||
                    !rule["pattern"].isString()) return false;
                const auto type = rule["type"].asString();
                if (type != "ip" && type != "cidr" && type != "domain" && type != "domainSuffix") return false;
                if (rule.isMember("action") && !rule["action"].isString()) return false;
                if (rule.isMember("enabled") && !rule["enabled"].isBool()) return false;
            }
        }
    }
    return true;
}

template <typename Handler>
Json::Value process_request(const std::string& line, Handler&& handler) {
    try {
        Json::CharReaderBuilder builder;
        builder["failIfExtra"] = true;
        builder["rejectDupKeys"] = true;
        builder["allowComments"] = false;
        builder["stackLimit"] = 64;
        Json::Value request;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (!reader->parse(line.data(), line.data() + line.size(), &request, &errors) ||
            !valid_request(request)) {
            return response(false, "JSON 请求或字段类型无效");
        }
        return handler(request);
    } catch (...) {
        // Exceptions may contain a profile or credentials. Do not echo them.
        return response(false, "请求处理失败");
    }
}

class RequestBuffer {
public:
    template <typename Handler>
    bool append(const char* data, std::size_t size, Handler&& handler) {
        input_.append(data, size);
        for (;;) {
            const auto newline = input_.find('\n');
            if (newline == std::string::npos) return input_.size() <= max_request_bytes;
            if (newline > max_request_bytes) return false;
            const auto line = input_.substr(0, newline);
            input_.erase(0, newline + 1);
            if (!line.empty() && !handler(line)) return false;
        }
    }

private:
    static constexpr std::size_t max_request_bytes = 1024 * 1024;
    std::string input_;
};

}  // namespace vpn::daemon
