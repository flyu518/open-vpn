class VpnCredentials {
  const VpnCredentials({
    required this.username,
    required this.password,
    this.remember = false,
  });

  final String username;
  final String password;
  final bool remember;
}
