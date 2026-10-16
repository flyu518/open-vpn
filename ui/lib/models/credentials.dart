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
