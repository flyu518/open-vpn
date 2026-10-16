#include <cassert>
#include <string>

#include "credential_codec.h"

int main() {
    const vpn::Credentials source{
        "用户\0A",
        "密码",
        "挑战响应",
        "私钥密码",
    };

    std::string encoded;
    assert(vpn::credential_codec::encode(source, &encoded));

    vpn::Credentials decoded;
    assert(vpn::credential_codec::decode(encoded, &decoded));
    assert(decoded.username == source.username);
    assert(decoded.password == source.password);
    assert(decoded.response == source.response);
    assert(decoded.private_key_password == source.private_key_password);

    encoded.push_back('x');
    assert(!vpn::credential_codec::decode(encoded, &decoded));
    return 0;
}
