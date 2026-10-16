#include "platform/macos/secure_store/keychain_store.h"

#include <Security/Security.h>

#include <memory>
#include <utility>

#include "credential_codec.h"

namespace vpn::platform::macos {
namespace {

struct CfRelease {
    void operator()(const void* value) const {
        if (value != nullptr) {
            CFRelease(value);
        }
    }
};

using CfPtr = std::unique_ptr<const void, CfRelease>;

CFStringRef make_string(const std::string& value) {
    return CFStringCreateWithCString(kCFAllocatorDefault,
                                     value.c_str(),
                                     kCFStringEncodingUTF8);
}

std::string status_text(OSStatus status) {
    CFStringRef message = SecCopyErrorMessageString(status, nullptr);
    if (message == nullptr) {
        return "Keychain 状态码 " + std::to_string(status);
    }
    char buffer[256] = {};
    if (!CFStringGetCString(message, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        CFRelease(message);
        return "Keychain 操作失败";
    }
    std::string result(buffer);
    CFRelease(message);
    return result;
}

CredentialStoreResult result(CredentialStoreStatus status, std::string diagnostic) {
    return {status, std::move(diagnostic)};
}

CredentialStoreResult map_status(OSStatus status, const char* operation) {
    if (status == errSecSuccess) {
        return result(CredentialStoreStatus::Ok, "操作成功");
    }
    if (status == errSecItemNotFound) {
        return result(CredentialStoreStatus::NotFound, "Keychain 中没有对应凭据");
    }
    if (status == errSecAuthFailed || status == errSecInteractionNotAllowed) {
        return result(CredentialStoreStatus::PermissionDenied,
                      std::string(operation) + "：Keychain 权限不足");
    }
    return result(CredentialStoreStatus::BackendError,
                  std::string(operation) + "：" + status_text(status));
}

CFMutableDictionaryRef base_query(const std::string& service,
                                   const std::string& account) {
    CFMutableDictionaryRef query =
        CFDictionaryCreateMutable(kCFAllocatorDefault,
                                  4,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    CfPtr service_value(make_string(service));
    CfPtr account_value(make_string(account));
    if (service_value == nullptr || account_value == nullptr) {
        CFRelease(query);
        return nullptr;
    }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service_value.get());
    CFDictionarySetValue(query, kSecAttrAccount, account_value.get());
    return query;
}

std::string account_for(std::string_view profile_id) {
    return "profile:" + std::string(profile_id);
}

}  // namespace

KeychainCredentialStore::KeychainCredentialStore(std::string service)
    : service_(std::move(service)) {}

vpn::CredentialStoreResult KeychainCredentialStore::save(
    std::string_view profile_id,
    const vpn::Credentials& credentials) {
    if (profile_id.empty()) {
        return result(CredentialStoreStatus::InvalidArgument, "profile 标识不能为空");
    }
    std::string encoded;
    if (!vpn::credential_codec::encode(credentials, &encoded)) {
        return result(CredentialStoreStatus::InvalidArgument, "凭据编码失败");
    }

    CFMutableDictionaryRef query = base_query(service_, account_for(profile_id));
    if (query == nullptr) {
        return result(CredentialStoreStatus::BackendError, "无法创建 Keychain 查询");
    }
    CfPtr value(CFDataCreate(kCFAllocatorDefault,
                             reinterpret_cast<const UInt8*>(encoded.data()),
                             static_cast<CFIndex>(encoded.size())));
    if (value == nullptr) {
        CFRelease(query);
        return result(CredentialStoreStatus::BackendError, "无法创建 Keychain 数据");
    }

    CFMutableDictionaryRef update =
        CFDictionaryCreateMutable(kCFAllocatorDefault,
                                  1,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(update, kSecValueData, value.get());
    OSStatus status = SecItemUpdate(query, update);
    CFRelease(update);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, value.get());
        CFDictionarySetValue(query,
                             kSecAttrAccessible,
                             kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        status = SecItemAdd(query, nullptr);
    }
    CFRelease(query);
    return map_status(status, "保存凭据");
}

vpn::CredentialStoreResult KeychainCredentialStore::load(
    std::string_view profile_id,
    vpn::Credentials* credentials) {
    if (profile_id.empty() || credentials == nullptr) {
        return result(CredentialStoreStatus::InvalidArgument, "读取凭据参数无效");
    }
    CFMutableDictionaryRef query = base_query(service_, account_for(profile_id));
    if (query == nullptr) {
        return result(CredentialStoreStatus::BackendError, "无法创建 Keychain 查询");
    }
    const void* return_data = kCFBooleanTrue;
    CFDictionarySetValue(query, kSecReturnData, return_data);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef raw = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &raw);
    CFRelease(query);
    if (status != errSecSuccess) {
        return map_status(status, "读取凭据");
    }
    CfPtr data(raw);
    if (CFGetTypeID(data.get()) != CFDataGetTypeID()) {
        return result(CredentialStoreStatus::BackendError, "Keychain 数据类型无效");
    }
    const auto* bytes = CFDataGetBytePtr(static_cast<CFDataRef>(data.get()));
    const CFIndex length = CFDataGetLength(static_cast<CFDataRef>(data.get()));
    if (!vpn::credential_codec::decode(
            std::string_view(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(length)),
            credentials)) {
        return result(CredentialStoreStatus::BackendError, "Keychain 数据格式无效");
    }
    return result(CredentialStoreStatus::Ok, "读取成功");
}

vpn::CredentialStoreResult KeychainCredentialStore::remove(std::string_view profile_id) {
    if (profile_id.empty()) {
        return result(CredentialStoreStatus::InvalidArgument, "删除凭据参数无效");
    }
    CFMutableDictionaryRef query = base_query(service_, account_for(profile_id));
    if (query == nullptr) {
        return result(CredentialStoreStatus::BackendError, "无法创建 Keychain 查询");
    }
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    if (status == errSecItemNotFound) {
        return result(CredentialStoreStatus::Ok, "凭据已经不存在");
    }
    return map_status(status, "删除凭据");
}

}  // namespace vpn::platform::macos
