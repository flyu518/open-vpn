import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/profile.dart';
import '../models/session.dart';
import '../models/route_strategy.dart';
import '../services/engine_client.dart';
import '../services/profile_store.dart';

class SessionViewModel extends ChangeNotifier {
  SessionViewModel({
    EngineClient? engineClient,
    ProfileStore? profileStore,
  })  : _engineClient = engineClient,
        _profileStore = profileStore {
    _engineSubscription = engineClient?.events.listen(_applyEngineEvent);
  }

  final Map<String, VpnSession> _sessions = <String, VpnSession>{};
  final Map<String, String> _errors = <String, String>{};
  final EngineClient? _engineClient;
  final ProfileStore? _profileStore;
  StreamSubscription<EngineEvent>? _engineSubscription;

  List<VpnSession> get sessions => List.unmodifiable(_sessions.values);

  String get activeLabel {
    final count = _sessions.values
        .where((session) => session.status == VpnStatus.connected)
        .length;
    return '$count 个连接活动中';
  }

  VpnSession? sessionById(String id) => _sessions[id];

  String? errorFor(String id) => _errors[id];

  void replaceSessions(Iterable<VpnSession> sessions) {
    _sessions
      ..clear()
      ..addEntries(sessions.map((session) => MapEntry(session.id, session)));
    notifyListeners();
  }

  void addImportedProfile(ImportedProfile profile) {
    _sessions[profile.id] = VpnSession(
      id: profile.id,
      name: profile.displayName,
      status: VpnStatus.disconnected,
      profileId: profile.id,
      profile: profile.contents,
      profilePath: profile.path,
      username: profile.username,
    );
    notifyListeners();
    unawaited(_saveProfiles());
  }

  Future<void> loadProfiles() async {
    final profileStore = _profileStore;
    if (profileStore == null) return;
    final restored = await profileStore.load();
    for (final session in restored) {
      _sessions[session.id] = session;
    }
    if (restored.isNotEmpty) notifyListeners();
  }

  Future<void> _saveProfiles() async {
    final profileStore = _profileStore;
    if (profileStore == null) return;
    try {
      await profileStore.save(_sessions.values);
    } on Object {
      // 配置持久化失败不影响当前连接，下一次导入或修改时继续尝试。
    }
  }

  void updateStatus(String id, VpnStatus status) {
    final session = _sessions[id];
    if (session == null) return;
    _sessions[id] = session.copyWith(status: status);
    notifyListeners();
  }

  void renameSession(String id, String name) {
    final session = _sessions[id];
    final normalizedName = name.trim();
    if (session == null || normalizedName.isEmpty) return;
    _sessions[id] = session.copyWith(name: normalizedName);
    notifyListeners();
    unawaited(_saveProfiles());
  }

  void applyError(String id, String message) {
    _errors[id] = message;
    notifyListeners();
  }

  void clearError(String id) {
    if (_errors.remove(id) != null) notifyListeners();
  }

  void _applyEngineEvent(EngineEvent event) {
    final session = _sessions[event.sessionId];
    if (session == null) return;

    _sessions[event.sessionId] = event.session == null
        ? session.copyWith(status: event.status)
        : session.copyWith(
            status: event.session!.status,
            remoteHost: event.session!.remoteHost,
            warnings: event.session!.warnings,
          );
    if (event.status == VpnStatus.failed && event.message.isNotEmpty) {
      _errors[event.sessionId] = event.message;
    } else if (event.status == VpnStatus.connecting) {
      _errors.remove(event.sessionId);
    }
    notifyListeners();
  }

  Future<void> connectSession(
    String id, {
    String username = '',
    String password = '',
    String response = '',
    String dynamicChallengeCookie = '',
    bool rememberCredentials = false,
  }) async {
    final session = _sessions[id];
    final engineClient = _engineClient;
    if (session == null || engineClient == null) return;

    _sessions[id] = session.copyWith(status: VpnStatus.connecting);
    _errors.remove(id);
    notifyListeners();

    try {
      final result = await engineClient.connect(
        id,
        profile: session.profile,
        username: username,
        password: password,
        response: response,
        dynamicChallengeCookie: dynamicChallengeCookie,
        profileId: session.profileId,
        rememberCredentials: rememberCredentials,
        routeStrategy: session.routeStrategy,
      );
      if (!result.ok) {
        _sessions[id] = session.copyWith(status: VpnStatus.failed);
        _errors[id] = result.message;
        notifyListeners();
        return;
      }
      if (result.session != null) {
        final current = _sessions[id] ?? session;
        _sessions[id] = _mergeRemote(current, result.session!);
        notifyListeners();
      }
    } on Object catch (error) {
      _sessions[id] = session.copyWith(status: VpnStatus.failed);
      _errors[id] = _errorMessage(error);
      notifyListeners();
    }
  }

  Future<void> disconnectSession(String id) async {
    final session = _sessions[id];
    final engineClient = _engineClient;
    if (session == null || engineClient == null) return;

    _sessions[id] = session.copyWith(status: VpnStatus.disconnecting);
    notifyListeners();
    try {
      final result = await engineClient.disconnect(id);
      if (!result.ok) {
        _sessions[id] = session.copyWith(status: VpnStatus.failed);
        _errors[id] = result.message;
        notifyListeners();
      }
    } on Object catch (error) {
      _sessions[id] = session.copyWith(status: VpnStatus.failed);
      _errors[id] = _errorMessage(error);
      notifyListeners();
    }
  }

  Future<void> loadSnapshot() async {
    final engineClient = _engineClient;
    if (engineClient == null) return;
    try {
      final result = await engineClient.snapshot();
      if (!result.ok) return;
      for (final remote in result.sessions) {
        final local = _sessions[remote.id];
        _sessions[remote.id] =
            local == null ? remote : _mergeRemote(local, remote);
      }
      notifyListeners();
    } on Object catch (_) {
      // daemon 尚未启动时保持空白界面，用户仍可先导入配置。
    }
  }

  String _errorMessage(Object error) {
    if (error is EngineClientException) return error.message;
    return 'OpenVPN 服务调用失败：$error';
  }

  VpnSession _mergeRemote(VpnSession local, VpnSession remote) {
    final keepConnected = local.status == VpnStatus.connected &&
        remote.status == VpnStatus.connecting;
    return local.copyWith(
      status: keepConnected ? local.status : remote.status,
      remoteHost: remote.remoteHost,
      interfaceName: remote.interfaceName,
      routes: remote.routes,
      downloadBytes: remote.downloadBytes,
      uploadBytes: remote.uploadBytes,
      duration: remote.duration,
      warnings: remote.warnings,
    );
  }

  void updateStrategy(String id, RouteStrategy strategy) {
    final session = _sessions[id];
    if (session == null) return;
    _sessions[id] = session.copyWith(routeStrategy: strategy);
    notifyListeners();
    unawaited(_saveProfiles());
  }

  void reorderSessions(int oldIndex, int newIndex) {
    final ordered = List<VpnSession>.from(_sessions.values);
    if (oldIndex < 0 || oldIndex >= ordered.length) return;
    if (newIndex < 0 || newIndex >= ordered.length) return;

    final moved = ordered.removeAt(oldIndex);
    ordered.insert(newIndex, moved);

    _sessions.clear();
    for (var index = 0; index < ordered.length; index++) {
      final session = ordered[index];
      final priority = ordered.length - index;
      _sessions[session.id] = session.copyWith(
        routeStrategy: session.routeStrategy.copyWith(priority: priority),
      );
    }
    notifyListeners();
    unawaited(_saveProfiles());
  }

  @override
  void dispose() {
    _engineSubscription?.cancel();
    super.dispose();
  }
}
