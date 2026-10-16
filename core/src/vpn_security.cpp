#include "vpn_security.h"

#include <regex>

namespace vpn {

std::string redactSecrets(std::string_view text) {
    static const std::regex secret_pattern(
        R"((password|token|private[_-]?key|secret|challenge)\s*[:=]\s*("[^"]*"|'[^']*'|[^\s,;]+))",
        std::regex_constants::icase);
    return std::regex_replace(std::string(text), secret_pattern, "$1=[已隐藏]");
}

}  // namespace vpn
