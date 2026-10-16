import 'dart:async';
import 'dart:convert';
import 'dart:io';

import '../models/session.dart';
import '../models/route_strategy.dart';

class EngineEvent {
  const EngineEvent({
    required this.sessionId,
    required this.status,
    this.message = '',
    this.session,
    this.serviceAvailable = true,
  });

  final String sessionId;
  final VpnStatus status;
  final String message;
  final VpnSession? session;
  final bool serviceAvailable;
}

class EngineResponse {
  const EngineResponse({
    required this.ok,
    this.message = '',
    this.session,
    this.sessions = const <VpnSession>[],
  });

  final bool ok;
  final String message;
  final VpnSession? session;
  final List<VpnSession> sessions;

  factory EngineResponse.fromJson(Map<String, dynamic> value) {
    final rawSessions = value['sessions'];
    final sessions = rawSessions is List
        ? rawSessions
            .whereType<Map>()
            .map((item) => VpnSession.fromJson(
                  Map<String, dynamic>.from(item),
                ))
            .toList(growable: false)
        : const <VpnSession>[];
    final rawSession = value['session'];
    return EngineResponse(
      ok: value['ok'] == true,
      message: value['message'] as String? ?? '',
      session: rawSession is Map
          ? VpnSession.fromJson(Map<String, dynamic>.from(rawSession))
          : null,
      sessions: sessions,
    );
  }
}

class EngineClientException implements Exception {
  const EngineClientException(this.message);

  final String message;

  @override
  String toString() => message;
}

abstract class EngineClient {
  Stream<EngineEvent> get events;

  Future<EngineResponse> connect(
    String sessionId, {
    String profile = '',
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    String profileId = '',
    bool rememberCredentials = false,
    RouteStrategy? routeStrategy,
  });

  Future<EngineResponse> disconnect(String sessionId);

  Future<EngineResponse> snapshot();

  Future<EngineResponse> forgetCredentials(String profileId);

  Future<void> importProfile();
}

/// macOS 桌面 daemon 的一行 JSON IPC 客户端。
/// Windows 版本将复用同一组命令，底层传输替换为受保护的 named pipe。
class UnixSocketEngineClient implements EngineClient {
  UnixSocketEngineClient({this.socketPath = '/tmp/openvpn-ui.sock'});

  final String socketPath;
  final StreamController<EngineEvent> _events =
      StreamController<EngineEvent>.broadcast();
  Socket? _socket;
  Future<void>? _connecting;
  final List<Completer<EngineResponse>> _pendingResponses =
      <Completer<EngineResponse>>[];

  @override
  Stream<EngineEvent> get events => _events.stream;

  Future<Socket> _ensureSocket() async {
    final existing = _socket;
    if (existing != null) return existing;
    final pending = _connecting;
    if (pending != null) {
      await pending;
      return _socket!;
    }
    final future = () async {
      try {
        final socket = await Socket.connect(
          InternetAddress(socketPath, type: InternetAddressType.unix),
          0,
        );
        _socket = socket;
        socket
            .cast<List<int>>()
            .transform(utf8.decoder)
            .transform(const LineSplitter())
            .listen(_handleLine, onDone: _handleClosed);
      } on SocketException {
        throw const EngineClientException('无法连接 OpenVPN 服务，请确认用户服务已经启动');
      } on Object {
        throw const EngineClientException('连接 OpenVPN 服务时发生未知错误');
      }
    }();
    _connecting = future;
    try {
      await future;
      return _socket!;
    } finally {
      _connecting = null;
    }
  }

  void _handleLine(String line) {
    final decoded = jsonDecode(line);
    if (decoded is! Map) return;
    final value = Map<String, dynamic>.from(decoded);
    if (value['type'] == 'sessionEvent') {
      final rawSession = value['session'];
      final session = rawSession is Map
          ? VpnSession.fromJson(Map<String, dynamic>.from(rawSession))
          : null;
      final sessionId = value['sessionId'] as String? ?? session?.id ?? '';
      _events.add(EngineEvent(
        sessionId: sessionId,
        status: session?.status ?? vpnStatusFromName(value['state'] as String?),
        message: value['message'] as String? ?? '',
        session: session,
      ));
      return;
    }

    if (_pendingResponses.isNotEmpty) {
      final pending = _pendingResponses.removeAt(0);
      pending.complete(EngineResponse.fromJson(value));
    }
  }

  void _handleClosed() {
    _socket = null;
    if (!_events.isClosed) {
      _events.add(const EngineEvent(
        sessionId: '',
        status: VpnStatus.disconnected,
        message: 'OpenVPN 服务连接已关闭',
        serviceAvailable: false,
      ));
    }
    final pending = List<Completer<EngineResponse>>.from(_pendingResponses);
    _pendingResponses.clear();
    for (final completer in pending) {
      if (!completer.isCompleted) {
        completer.completeError(
          const EngineClientException('OpenVPN 服务连接已关闭'),
        );
      }
    }
  }

  Future<EngineResponse> _send(Map<String, Object?> request) async {
    final socket = await _ensureSocket();
    final completer = Completer<EngineResponse>();
    _pendingResponses.add(completer);
    socket.write('${jsonEncode(request)}\n');
    await socket.flush();
    try {
      return await completer.future.timeout(
        const Duration(seconds: 20),
        onTimeout: () => throw const EngineClientException('OpenVPN 服务响应超时'),
      );
    } catch (_) {
      _pendingResponses.remove(completer);
      rethrow;
    }
  }

  @override
  Future<EngineResponse> connect(
    String sessionId, {
    String profile = '',
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    String profileId = '',
    bool rememberCredentials = false,
    RouteStrategy? routeStrategy,
  }) =>
      _send({
        'command': 'connect',
        'sessionId': sessionId,
        'profileId': profileId.isEmpty ? sessionId : profileId,
        'name': sessionId,
        'profile': profile,
        'credentials': {
          'username': username,
          'password': password,
          'response': response,
          'dynamicChallengeCookie': dynamicChallengeCookie,
        },
        'rememberCredentials': rememberCredentials,
        'routeStrategy': (routeStrategy ?? const RouteStrategy()).toJson(),
      });

  @override
  Future<EngineResponse> disconnect(String sessionId) =>
      _send({'command': 'disconnect', 'sessionId': sessionId});

  @override
  Future<EngineResponse> snapshot() => _send({'command': 'snapshot'});

  @override
  Future<EngineResponse> forgetCredentials(String profileId) =>
      _send({'command': 'forgetCredentials', 'profileId': profileId});

  @override
  Future<void> importProfile() async {
    // 文件选择和 profile/凭据组装由桌面界面完成。
  }

  Future<void> dispose() async {
    await _socket?.close();
    await _events.close();
  }
}

/// UI 开发阶段使用的内存实现；真正的实现通过桌面 IPC 调用 C++ 引擎。
class InMemoryEngineClient implements EngineClient {
  final StreamController<EngineEvent> _events =
      StreamController<EngineEvent>.broadcast();

  @override
  Stream<EngineEvent> get events => _events.stream;

  void pushEvent(EngineEvent event) {
    _events.add(event);
  }

  @override
  Future<EngineResponse> connect(
    String sessionId, {
    String profile = '',
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    String profileId = '',
    bool rememberCredentials = false,
    RouteStrategy? routeStrategy,
  }) async {
    _events.add(EngineEvent(
      sessionId: sessionId,
      status: VpnStatus.connecting,
    ));
    return const EngineResponse(ok: true, message: '连接请求已提交');
  }

  @override
  Future<EngineResponse> disconnect(String sessionId) async {
    _events.add(EngineEvent(
      sessionId: sessionId,
      status: VpnStatus.disconnected,
    ));
    return const EngineResponse(ok: true, message: '断开请求已提交');
  }

  @override
  Future<EngineResponse> snapshot() async =>
      const EngineResponse(ok: true, sessions: <VpnSession>[]);

  @override
  Future<EngineResponse> forgetCredentials(String profileId) async =>
      const EngineResponse(ok: true, message: '凭据已删除');

  @override
  Future<void> importProfile() async {}

  Future<void> dispose() => _events.close();
}

/// Windows named-pipe 客户端接入前使用的明确失败实现，避免把界面演示状态误认为真实连接。
class UnavailableEngineClient implements EngineClient {
  UnavailableEngineClient(this.reason);

  final String reason;
  final StreamController<EngineEvent> _events =
      StreamController<EngineEvent>.broadcast();

  @override
  Stream<EngineEvent> get events => _events.stream;

  @override
  Future<EngineResponse> connect(
    String sessionId, {
    String profile = '',
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    String profileId = '',
    bool rememberCredentials = false,
    RouteStrategy? routeStrategy,
  }) async =>
      EngineResponse(ok: false, message: reason);

  @override
  Future<EngineResponse> disconnect(String sessionId) async =>
      EngineResponse(ok: false, message: reason);

  @override
  Future<EngineResponse> snapshot() async =>
      EngineResponse(ok: false, message: reason);

  @override
  Future<EngineResponse> forgetCredentials(String profileId) async =>
      EngineResponse(ok: false, message: reason);

  @override
  Future<void> importProfile() async {}

  Future<void> dispose() => _events.close();
}
