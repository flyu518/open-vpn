#ifndef VPN_CREDENTIAL_CODEC_H
#define VPN_CREDENTIAL_CODEC_H

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "vpn_credential_store.h"

namespace vpn::credential_codec {

namespace detail {

inline void append_u32(std::string& output, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

inline bool read_u32(std::string_view input,
                     std::size_t* offset,
                     std::uint32_t* value) {
    if (*offset > input.size() || input.size() - *offset < 4) {
        return false;
    }
    const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(input.data() + *offset);
    *value = static_cast<std::uint32_t>(bytes[0]) |
             (static_cast<std::uint32_t>(bytes[1]) << 8) |
             (static_cast<std::uint32_t>(bytes[2]) << 16) |
             (static_cast<std::uint32_t>(bytes[3]) << 24);
    *offset += 4;
    return true;
}

inline bool append_field(std::string& output, std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    append_u32(output, static_cast<std::uint32_t>(value.size()));
    output.append(value.data(), value.size());
    return true;
}

inline bool read_field(std::string_view input,
                       std::size_t* offset,
                       std::string* value) {
    std::uint32_t length = 0;
    if (!read_u32(input, offset, &length) ||
        length > input.size() - *offset) {
        return false;
    }
    value->assign(input.data() + *offset, length);
    *offset += length;
    return true;
}

}  // namespace detail

inline bool encode(const Credentials& credentials, std::string* output) {
    if (output == nullptr) {
        return false;
    }
    output->clear();
    output->reserve(credentials.username.size() + credentials.password.size() +
                    credentials.response.size() + credentials.private_key_password.size() +
                    16);
    return detail::append_field(*output, credentials.username) &&
           detail::append_field(*output, credentials.password) &&
           detail::append_field(*output, credentials.response) &&
           detail::append_field(*output, credentials.private_key_password);
}

inline bool decode(std::string_view input, Credentials* credentials) {
    if (credentials == nullptr) {
        return false;
    }
    Credentials decoded;
    std::size_t offset = 0;
    if (!detail::read_field(input, &offset, &decoded.username) ||
        !detail::read_field(input, &offset, &decoded.password) ||
        !detail::read_field(input, &offset, &decoded.response) ||
        !detail::read_field(input, &offset, &decoded.private_key_password) ||
        offset != input.size()) {
        return false;
    }
    *credentials = std::move(decoded);
    return true;
}

}  // namespace vpn::credential_codec

#endif
