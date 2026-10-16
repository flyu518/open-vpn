#include "platform/windows/secure_store/credential_store.h"

#ifdef _WIN32

#include <windows.h>
#include <wincred.h>

#include <utility>

#include "credential_codec.h"

namespace vpn::platform::windows {
namespace {

std::wstring wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8,
                                           MB_ERR_INVALID_CHARS,
                                           value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr,
                                           0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            value.data(),
                            static_cast<int>(value.size()),
                            result.data(),
                            length) != length) {
        return {};
    }
    return result;
}

CredentialStoreResult win_error(const char* operation) {
    const DWORD error = GetLastError();
    if (error == ERROR_NOT_FOUND || error == ERROR_FILE_NOT_FOUND) {
        return {CredentialStoreStatus::NotFound, "Credential Manager 中没有对应凭据"};
    }
    if (error == ERROR_ACCESS_DENIED) {
        return {CredentialStoreStatus::PermissionDenied,
                std::string(operation) + "：Credential Manager 权限不足"};
    }
    return {CredentialStoreStatus::BackendError,
            std::string(operation) + "失败，错误码 " + std::to_string(error)};
}

std::wstring target_name(const std::string& prefix, std::string_view profile_id) {
    return wide(prefix + std::string(profile_id));
}

}  // namespace

WindowsCredentialStore::WindowsCredentialStore(std::string target_prefix)
    : target_prefix_(std::move(target_prefix)) {}

vpn::CredentialStoreResult WindowsCredentialStore::save(
    std::string_view profile_id,
    const vpn::Credentials& credentials) {
    if (profile_id.empty()) {
        return {CredentialStoreStatus::InvalidArgument, "profile 标识不能为空"};
    }
    std::string encoded;
    if (!vpn::credential_codec::encode(credentials, &encoded)) {
        return {CredentialStoreStatus::InvalidArgument, "凭据编码失败"};
    }
    const std::wstring target = target_name(target_prefix_, profile_id);
    if (target.empty()) {
        return {CredentialStoreStatus::InvalidArgument, "profile 标识不是有效 UTF-8"};
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.CredentialBlobSize = static_cast<DWORD>(encoded.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(encoded.data());
    if (!CredWriteW(&credential, 0)) {
        return win_error("保存凭据");
    }
    return {CredentialStoreStatus::Ok, "保存成功"};
}

vpn::CredentialStoreResult WindowsCredentialStore::load(
    std::string_view profile_id,
    vpn::Credentials* credentials) {
    if (profile_id.empty() || credentials == nullptr) {
        return {CredentialStoreStatus::InvalidArgument, "读取凭据参数无效"};
    }
    const std::wstring target = target_name(target_prefix_, profile_id);
    PCREDENTIALW raw = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
        return win_error("读取凭据");
    }
    std::string_view encoded(reinterpret_cast<const char*>(raw->CredentialBlob),
                             raw->CredentialBlobSize);
    const bool decoded = vpn::credential_codec::decode(encoded, credentials);
    CredFree(raw);
    if (!decoded) {
        return {CredentialStoreStatus::BackendError, "Credential Manager 数据格式无效"};
    }
    return {CredentialStoreStatus::Ok, "读取成功"};
}

vpn::CredentialStoreResult WindowsCredentialStore::remove(std::string_view profile_id) {
    if (profile_id.empty()) {
        return {CredentialStoreStatus::InvalidArgument, "删除凭据参数无效"};
    }
    const std::wstring target = target_name(target_prefix_, profile_id);
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        if (GetLastError() == ERROR_NOT_FOUND) {
            return {CredentialStoreStatus::Ok, "凭据已经不存在"};
        }
        return win_error("删除凭据");
    }
    return {CredentialStoreStatus::Ok, "删除成功"};
}

}  // namespace vpn::platform::windows

#endif
