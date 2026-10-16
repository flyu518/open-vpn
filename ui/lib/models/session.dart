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
    this.profilePath = '',
    this.username = '',
    this.remoteHost = '',
    this.interfaceName = '',
    this.routes = const <String>[],
    this.downloadBytes = 0,
    this.uploadBytes = 0,
    this.duration = Duration.zero,
    this.routeStrategy = const RouteStrategy(),
    this.warnings = const <String>[],
    this.connectionCount = 0,
    this.lastConnectedAt,
    this.lastDisconnectedAt,
  });

  final String id;
  final String name;
  final VpnStatus status;

  /// 只保留在当前进程内，用于向 daemon 提交连接请求，不写入日志或快照。
  final String profileId;
  final String profile;
  final String profilePath;
  final String username;
  final String remoteHost;
  final String interfaceName;
  final List<String> routes;
  final int downloadBytes;
  final int uploadBytes;
  final Duration duration;
  final RouteStrategy routeStrategy;
  final List<String> warnings;

  /// 轻量的连接活动摘要，跟随配置保存；不保存密码、证书或完整日志。
  final int connectionCount;
  final DateTime? lastConnectedAt;
  final DateTime? lastDisconnectedAt;

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
    String? profilePath,
    String? username,
    String? remoteHost,
    String? interfaceName,
    List<String>? routes,
    int? downloadBytes,
    int? uploadBytes,
    Duration? duration,
    RouteStrategy? routeStrategy,
    List<String>? warnings,
    int? connectionCount,
    DateTime? lastConnectedAt,
    DateTime? lastDisconnectedAt,
  }) {
    return VpnSession(
      id: id,
      name: name ?? this.name,
      status: status ?? this.status,
      profileId: profileId ?? this.profileId,
      profile: profile ?? this.profile,
      profilePath: profilePath ?? this.profilePath,
      username: username ?? this.username,
      remoteHost: remoteHost ?? this.remoteHost,
      interfaceName: interfaceName ?? this.interfaceName,
      routes: routes ?? this.routes,
      downloadBytes: downloadBytes ?? this.downloadBytes,
      uploadBytes: uploadBytes ?? this.uploadBytes,
      duration: duration ?? this.duration,
      routeStrategy: routeStrategy ?? this.routeStrategy,
      warnings: warnings ?? this.warnings,
      connectionCount: connectionCount ?? this.connectionCount,
      lastConnectedAt: lastConnectedAt ?? this.lastConnectedAt,
      lastDisconnectedAt: lastDisconnectedAt ?? this.lastDisconnectedAt,
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
