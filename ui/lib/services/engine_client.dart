import 'dart:async';
import 'dart:convert';
import 'dart:io';

import '../models/session.dart';

class EngineEvent {
  const EngineEvent({
    required this.sessionId,
    required this.status,
    this.message = '',
  });

  final String sessionId;
  final VpnStatus status;
  final String message;
}

abstract class EngineClient {
  Stream<EngineEvent> get events;

  Future<void> connect(
    String sessionId, {
    String profile = '',
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    String profileId = '',
    bool rememberCredentials = false,
  });

  Future<void> disconnect(String sessionId);

  Future<void> importProfile();
}

/// macOS 桌面 daemon 的一行 JSON IPC 客户端。
/// Windows 版本将复用同一组命令，底层传输替换为受保护的 named pipe。
class UnixSocketEngineClient implements EngineClient {
  UnixSocketEngineClient({this.socketPath = '/tmp/vpn-ui.sock'});

  final String socketPath;
  final StreamController<EngineEvent> _events =
      StreamController<EngineEvent>.broadcast();
  Socket? _socket;
  Future<void>? _connecting;

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
      final socket = await Socket.connect(
        InternetAddress(socketPath, type: InternetAddressType.unix),
        0,
      );
      _socket = socket;
      socket
          .transform(utf8.decoder)
          .transform(const LineSplitter())
          .listen(_handleLine, onDone: _handleClosed);
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
    final value = jsonDecode(line);
    if (value is! Map<String, dynamic> || value['type'] != 'sessionEvent') {
      return;
    }
    final state = _statusFromName(value['session']?['state'] as String?);
    _events.add(EngineEvent(
      sessionId: value['sessionId'] as String? ?? '',
      status: state,
      message: value['message'] as String? ?? '',
    ));
  }

  VpnStatus _statusFromName(String? value) {
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

  void _handleClosed() {
    _socket = null;
  }

  Future<void> _send(Map<String, Object?> request) async {
    final socket = await _ensureSocket();
    socket.write('${jsonEncode(request)}\n');
    await socket.flush();
  }

  @override
  Future<void> connect(
    String sessionId, {
    String profile = '',
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    String profileId = '',
    bool rememberCredentials = false,
  }) => _send({
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
      });

  @override
  Future<void> disconnect(String sessionId) =>
      _send({'command': 'disconnect', 'sessionId': sessionId});

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

  @override
  Future<void> connect(
    String sessionId, {
    String profile = '',
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    String profileId = '',
    bool rememberCredentials = false,
  }) async {
    _events.add(EngineEvent(
      sessionId: sessionId,
      status: VpnStatus.connecting,
    ));
  }

  @override
  Future<void> disconnect(String sessionId) async {
    _events.add(EngineEvent(
      sessionId: sessionId,
      status: VpnStatus.disconnected,
    ));
  }

  @override
  Future<void> importProfile() async {}

  Future<void> dispose() => _events.close();
}
