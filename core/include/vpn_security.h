#ifndef VPN_SECURITY_H
#define VPN_SECURITY_H

#include <string>
#include <string_view>

namespace vpn {

std::string redactSecrets(std::string_view text);

}  // namespace vpn

#endif
