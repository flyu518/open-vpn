import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'dart:async';

import 'package:openvpn_ui/models/session.dart';
import 'package:openvpn_ui/models/route_strategy.dart';
import 'package:openvpn_ui/services/engine_client.dart';
import 'package:openvpn_ui/view_models/session_view_model.dart';

void main() {
  test('桌面端两个 VPN 会话显示为互相独立的卡片', () {
    final viewModel = SessionViewModel();

    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      const VpnSession(
          id: 'office-b', name: '财务网络', status: VpnStatus.connecting),
    ]);

    expect(viewModel.sessions.map((session) => session.id),
        containsAll(<String>['office-a', 'office-b']));
    expect(viewModel.activeLabel, '1 个连接活动中');
  });

  test('路由冲突只提示错误，不移除已有连接', () {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.connected),
    ]);

    viewModel.applyError('office-b', '路由与活动 VPN 重叠');

    expect(viewModel.sessions.single.id, 'office-a');
    expect(viewModel.errorFor('office-b'), '路由与活动 VPN 重叠');
  });

  test('事件只更新对应会话', () {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      const VpnSession(
          id: 'office-b', name: '财务网络', status: VpnStatus.connecting),
    ]);

    viewModel.updateStatus('office-b', VpnStatus.failed);

    expect(viewModel.sessionById('office-a')!.status, VpnStatus.connected);
    expect(viewModel.sessionById('office-b')!.status, VpnStatus.failed);
  });

  test('注入的引擎事件只更新对应会话', () async {
    final engine = InMemoryEngineClient();
    final viewModel = SessionViewModel(engineClient: engine);
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      const VpnSession(
          id: 'office-b', name: '财务网络', status: VpnStatus.connecting),
    ]);

    engine.pushEvent(const EngineEvent(
      sessionId: 'office-b',
      status: VpnStatus.failed,
      message: '路由与活动 VPN 重叠',
    ));
    await Future<void>.delayed(Duration.zero);

    expect(viewModel.sessionById('office-a')!.status, VpnStatus.connected);
    expect(viewModel.sessionById('office-b')!.status, VpnStatus.failed);
    expect(viewModel.errorFor('office-b'), '路由与活动 VPN 重叠');

    viewModel.dispose();
    await engine.dispose();
  });

  test('连接请求失败时只在当前会话显示错误', () async {
    final engine = _RejectingEngineClient('路由与活动 VPN 重叠：office-a');
    final viewModel = SessionViewModel(engineClient: engine);
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      const VpnSession(
          id: 'office-b', name: '财务网络', status: VpnStatus.disconnected),
    ]);

    await viewModel.connectSession('office-b',
        username: 'user', password: 'secret');

    expect(viewModel.sessionById('office-a')!.status, VpnStatus.connected);
    expect(viewModel.sessionById('office-b')!.status, VpnStatus.failed);
    expect(viewModel.errorFor('office-b'), '路由与活动 VPN 重叠：office-a');

    viewModel.dispose();
    await engine.dispose();
  });

  test('daemon 不可用时连接卡片不会停在正在连接', () async {
    final engine = UnixSocketEngineClient(
      socketPath: '/tmp/vpn-ui-missing-$pid.sock',
    );
    final viewModel = SessionViewModel(engineClient: engine);
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.disconnected),
    ]);

    await viewModel.connectSession('office-a',
        username: 'user', password: 'secret');

    expect(viewModel.sessionById('office-a')!.status, VpnStatus.failed);
    expect(viewModel.errorFor('office-a'), isNotEmpty);

    viewModel.dispose();
    await engine.dispose();
  });

  test('已连接事件不会被较晚的连接提交响应覆盖', () async {
    final engine = _RacingEngineClient();
    final viewModel = SessionViewModel(engineClient: engine);
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.disconnected),
    ]);

    await viewModel.connectSession('office-a',
        username: 'user', password: 'secret');

    expect(viewModel.sessionById('office-a')!.status, VpnStatus.connected);

    viewModel.dispose();
    await engine.dispose();
  });

  test('每个会话可以独立保存路由冲突策略', () {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      const VpnSession(
          id: 'office-b', name: '财务网络', status: VpnStatus.disconnected),
    ]);

    viewModel.updateStrategy(
      'office-b',
      const RouteStrategy(
        conflictMode: RouteConflictMode.ask,
        priority: 10,
        rules: [
          RouteRule(
              type: RouteRuleType.domainSuffix, pattern: '*.corp.example.com')
        ],
      ),
    );

    expect(viewModel.sessionById('office-a')!.routeStrategy.conflictMode,
        RouteConflictMode.compatible);
    expect(viewModel.sessionById('office-b')!.routeStrategy.conflictMode,
        RouteConflictMode.ask);
    expect(viewModel.sessionById('office-b')!.routeStrategy.priority, 10);
  });

  test('拖动连接顺序会更新优先级', () {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
          id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      const VpnSession(
          id: 'office-b', name: '财务网络', status: VpnStatus.connected),
    ]);

    viewModel.reorderSessions(1, 0);

    expect(viewModel.sessions.first.id, 'office-b');
    expect(viewModel.sessionById('office-b')!.routeStrategy.priority, 2);
    expect(viewModel.sessionById('office-a')!.routeStrategy.priority, 1);
  });
}

class _RejectingEngineClient implements EngineClient {
  _RejectingEngineClient(this.message);

  final String message;
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
  }) async {
    return EngineResponse(ok: false, message: message);
  }

  @override
  Future<EngineResponse> disconnect(String sessionId) async =>
      const EngineResponse(ok: true);

  @override
  Future<EngineResponse> snapshot() async =>
      const EngineResponse(ok: true, sessions: <VpnSession>[]);

  @override
  Future<EngineResponse> forgetCredentials(String profileId) async =>
      const EngineResponse(ok: true);

  @override
  Future<void> importProfile() async {}

  Future<void> dispose() => _events.close();
}

class _RacingEngineClient implements EngineClient {
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
  }) async {
    _events.add(const EngineEvent(
      sessionId: 'office-a',
      status: VpnStatus.connected,
    ));
    await Future<void>.delayed(Duration.zero);
    return const EngineResponse(
      ok: true,
      session: VpnSession(
        id: 'office-a',
        name: '研发网络',
        status: VpnStatus.connecting,
      ),
    );
  }

  @override
  Future<EngineResponse> disconnect(String sessionId) async =>
      const EngineResponse(ok: true);

  @override
  Future<EngineResponse> snapshot() async =>
      const EngineResponse(ok: true, sessions: <VpnSession>[]);

  @override
  Future<EngineResponse> forgetCredentials(String profileId) async =>
      const EngineResponse(ok: true);

  @override
  Future<void> importProfile() async {}

  Future<void> dispose() => _events.close();
}
