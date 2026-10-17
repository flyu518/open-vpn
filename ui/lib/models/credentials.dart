class VpnCredentials {
  const VpnCredentials({
    required this.username,
    required this.password,
    this.remember = false,
    this.useSaved = false,
  });

  final String username;
  final String password;
  final bool remember;
  final bool useSaved;
}

bool isCredentialFailureMessage(String message) {
  final lower = message.toLowerCase();
  return message.contains('凭据') ||
      message.contains('认证') ||
      message.contains('密码') ||
      message.contains('用户名') ||
      lower.contains('auth_failed') ||
      lower.contains('authentication') ||
      lower.contains('credential') ||
      lower.contains('keychain') ||
      lower.contains('credential manager');
}
