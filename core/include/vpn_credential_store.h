#ifndef VPN_CREDENTIAL_STORE_H
#define VPN_CREDENTIAL_STORE_H

#include <string>
#include <string_view>

namespace vpn {

struct Credentials {
    std::string username;
    std::string password;
    std::string response;
    std::string private_key_password;
};

enum class CredentialStoreStatus {
    Ok,
    NotFound,
    InvalidArgument,
    PermissionDenied,
    BackendError
};

struct CredentialStoreResult {
    CredentialStoreStatus status = CredentialStoreStatus::BackendError;
    std::string diagnostic;

    bool ok() const {
        return status == CredentialStoreStatus::Ok;
    }
};

class CredentialStore {
public:
    virtual ~CredentialStore() = default;

    virtual CredentialStoreResult save(std::string_view profile_id,
                                       const Credentials& credentials) = 0;
    virtual CredentialStoreResult load(std::string_view profile_id,
                                       Credentials* credentials) = 0;
    virtual CredentialStoreResult remove(std::string_view profile_id) = 0;
};

}  // namespace vpn

#endif
