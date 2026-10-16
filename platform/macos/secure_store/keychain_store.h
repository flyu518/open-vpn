#ifndef VPN_MACOS_KEYCHAIN_STORE_H
#define VPN_MACOS_KEYCHAIN_STORE_H

#include <string>

#include "vpn_credential_store.h"

namespace vpn::platform::macos {

class KeychainCredentialStore final : public vpn::CredentialStore {
public:
    explicit KeychainCredentialStore(std::string service = "com.openvpn.credentials");

    vpn::CredentialStoreResult save(std::string_view profile_id,
                                    const vpn::Credentials& credentials) override;
    vpn::CredentialStoreResult load(std::string_view profile_id,
                                    vpn::Credentials* credentials) override;
    vpn::CredentialStoreResult remove(std::string_view profile_id) override;

private:
    std::string service_;
};

}  // namespace vpn::platform::macos

#endif
