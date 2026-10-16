import 'dart:async';

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

  Future<void> connect(String sessionId);

  Future<void> disconnect(String sessionId);

  Future<void> importProfile();
}

/// UI 开发阶段使用的内存实现；真正的实现通过桌面 IPC 调用 C++ 引擎。
class InMemoryEngineClient implements EngineClient {
  final StreamController<EngineEvent> _events =
      StreamController<EngineEvent>.broadcast();

  @override
  Stream<EngineEvent> get events => _events.stream;

  @override
  Future<void> connect(String sessionId) async {
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
