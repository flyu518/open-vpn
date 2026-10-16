#ifndef VPN_WINDOWS_CREDENTIAL_STORE_H
#define VPN_WINDOWS_CREDENTIAL_STORE_H

#include <string>

#include "vpn_credential_store.h"

namespace vpn::platform::windows {

class WindowsCredentialStore final : public vpn::CredentialStore {
public:
    explicit WindowsCredentialStore(std::string target_prefix = "vpn/");

    vpn::CredentialStoreResult save(std::string_view profile_id,
                                    const vpn::Credentials& credentials) override;
    vpn::CredentialStoreResult load(std::string_view profile_id,
                                    vpn::Credentials* credentials) override;
    vpn::CredentialStoreResult remove(std::string_view profile_id) override;

private:
    std::string target_prefix_;
};

}  // namespace vpn::platform::windows

#endif
