import 'route_strategy.dart';

enum VpnStatus {
  disconnected,
  connecting,
  connected,
  disconnecting,
  failed,
}

extension VpnStatusText on VpnStatus {
  String get label {
    switch (this) {
      case VpnStatus.disconnected:
        return '未连接';
      case VpnStatus.connecting:
        return '正在连接';
      case VpnStatus.connected:
        return '已连接';
      case VpnStatus.disconnecting:
        return '正在断开';
      case VpnStatus.failed:
        return '连接失败';
    }
  }
}

class VpnSession {
  const VpnSession({
    required this.id,
    required this.name,
    required this.status,
    this.profileId = '',
    this.profile = '',
    this.username = '',
    this.remoteHost = '',
    this.interfaceName = '',
    this.routes = const <String>[],
    this.downloadBytes = 0,
    this.uploadBytes = 0,
    this.duration = Duration.zero,
    this.routeStrategy = const RouteStrategy(),
    this.warnings = const <String>[],
  });

  final String id;
  final String name;
  final VpnStatus status;

  /// 只保留在当前进程内，用于向 daemon 提交连接请求，不写入日志或快照。
  final String profileId;
  final String profile;
  final String username;
  final String remoteHost;
  final String interfaceName;
  final List<String> routes;
  final int downloadBytes;
  final int uploadBytes;
  final Duration duration;
  final RouteStrategy routeStrategy;
  final List<String> warnings;

  factory VpnSession.fromJson(Map<String, dynamic> value) {
    final rawWarnings = value['warnings'];
    return VpnSession(
      id: value['sessionId'] as String? ?? '',
      name: value['name'] as String? ?? value['sessionId'] as String? ?? '',
      status: vpnStatusFromName(value['state'] as String?),
      remoteHost: value['remoteHost'] as String? ?? '',
      warnings: rawWarnings is List
          ? rawWarnings.whereType<String>().toList(growable: false)
          : const <String>[],
    );
  }

  VpnSession copyWith({
    String? name,
    VpnStatus? status,
    String? profileId,
    String? profile,
    String? username,
    String? remoteHost,
    String? interfaceName,
    List<String>? routes,
    int? downloadBytes,
    int? uploadBytes,
    Duration? duration,
    RouteStrategy? routeStrategy,
    List<String>? warnings,
  }) {
    return VpnSession(
      id: id,
      name: name ?? this.name,
      status: status ?? this.status,
      profileId: profileId ?? this.profileId,
      profile: profile ?? this.profile,
      username: username ?? this.username,
      remoteHost: remoteHost ?? this.remoteHost,
      interfaceName: interfaceName ?? this.interfaceName,
      routes: routes ?? this.routes,
      downloadBytes: downloadBytes ?? this.downloadBytes,
      uploadBytes: uploadBytes ?? this.uploadBytes,
      duration: duration ?? this.duration,
      routeStrategy: routeStrategy ?? this.routeStrategy,
      warnings: warnings ?? this.warnings,
    );
  }
}

VpnStatus vpnStatusFromName(String? value) {
  switch (value) {
    case 'connected':
      return VpnStatus.connected;
    case 'connecting':
      return VpnStatus.connecting;
    case 'disconnecting':
      return VpnStatus.disconnecting;
    case 'failed':
      return VpnStatus.failed;
    default:
      return VpnStatus.disconnected;
  }
}
