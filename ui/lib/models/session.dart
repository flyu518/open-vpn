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
  final String remoteHost;
  final String interfaceName;
  final List<String> routes;
  final int downloadBytes;
  final int uploadBytes;
  final Duration duration;
  final RouteStrategy routeStrategy;
  final List<String> warnings;

  VpnSession copyWith({
    String? name,
    VpnStatus? status,
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
