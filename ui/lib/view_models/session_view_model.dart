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
  bool _serviceChecked = false;
  String? _serviceError;
  String? _profileStoreError;

  List<VpnSession> get sessions => List.unmodifiable(_sessions.values);

  String get activeLabel {
    final count = _sessions.values
        .where((session) => session.status == VpnStatus.connected)
        .length;
    return '$count 个连接活动中';
  }

  VpnSession? sessionById(String id) => _sessions[id];

  String? errorFor(String id) => _errors[id];

  String? get serviceError => _serviceError;

  bool get serviceChecked => _serviceChecked;

  String? get profileStoreError => _profileStoreError;

  void replaceSessions(Iterable<VpnSession> sessions) {
    _sessions
      ..clear()
      ..addEntries(sessions.map((session) => MapEntry(session.id, session)));
    notifyListeners();
  }

  Future<void> addImportedProfile(ImportedProfile profile) async {
    final existing = _sessions[profile.id];
    final imported = VpnSession(
      id: profile.id,
      name: profile.displayName,
      status: VpnStatus.disconnected,
      profileId: profile.id,
      profile: profile.contents,
      profilePath: profile.path,
      username: profile.username,
    );
    _sessions[profile.id] = existing == null
        ? imported
        : existing.copyWith(
            name: _keepSessionName(existing.name)
                ? existing.name
                : profile.displayName,
            profileId: profile.id,
            profile: profile.contents,
            profilePath: profile.path,
            username: profile.username,
          );
    notifyListeners();
    await _saveProfiles(throwOnError: true);
  }

  Future<void> replaceImportedProfile(
      String sessionId, ImportedProfile profile) async {
    final existing = _sessions[sessionId];
    if (existing == null) return;
    _sessions[sessionId] = existing.copyWith(
      name: _keepSessionName(existing.name) ? existing.name : profile.displayName,
      profileId: existing.profileId.isEmpty ? profile.id : existing.profileId,
      profile: profile.contents,
      profilePath: profile.path,
      username: profile.username,
    );
    notifyListeners();
    await _saveProfiles(throwOnError: true);
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

  Future<void> _saveProfiles({bool throwOnError = false}) async {
    final profileStore = _profileStore;
    if (profileStore == null) return;
    try {
      await profileStore.save(_sessions.values);
      if (_profileStoreError != null) {
        _profileStoreError = null;
        notifyListeners();
      }
    } on Object {
      _profileStoreError = '配置保存失败，请检查配置目录权限后重试';
      notifyListeners();
      if (throwOnError) rethrow;
    }
  }

  void updateStatus(String id, VpnStatus status) {
    final session = _sessions[id];
    if (session == null) return;

    final updated = _recordConnectionTransition(
      session,
      session.copyWith(status: status),
    );
    _sessions[id] = updated;
    if (_activityChanged(session, updated)) unawaited(_saveProfiles());
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
    if (!event.serviceAvailable) {
      _serviceChecked = true;
      _serviceError = event.message.isEmpty ? 'OpenVPN 服务不可用' : event.message;
      for (final entry in _sessions.entries.toList()) {
        final status = entry.value.status;
        if (status == VpnStatus.connected ||
            status == VpnStatus.connecting ||
            status == VpnStatus.disconnecting) {
          _sessions[entry.key] = entry.value.copyWith(status: VpnStatus.failed);
          _errors[entry.key] = _serviceError!;
        }
      }
      notifyListeners();
      return;
    }
    final session = _sessions[event.sessionId];
    if (session == null) return;

    _serviceChecked = true;
    _serviceError = null;
    final next = event.session == null
        ? session.copyWith(status: event.status)
        : session.copyWith(
            status: event.session!.status,
            remoteHost: event.session!.remoteHost,
            warnings: event.session!.warnings,
          );
    final updated = _recordConnectionTransition(session, next);
    _sessions[event.sessionId] = updated;
    if (_activityChanged(session, updated)) unawaited(_saveProfiles());
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
        _serviceChecked = true;
        _sessions[id] = session.copyWith(status: VpnStatus.failed);
        _errors[id] = result.message;
        if (_looksLikeServiceError(result.message)) {
          _serviceError = result.message;
        }
        notifyListeners();
        return;
      }
      if (result.session != null) {
        _serviceChecked = true;
        _serviceError = null;
        final current = _sessions[id] ?? session;
        final merged = _mergeRemote(current, result.session!);
        final updated = _recordConnectionTransition(current, merged);
        _sessions[id] = updated;
        if (_activityChanged(current, updated)) unawaited(_saveProfiles());
        notifyListeners();
      }
    } on Object catch (error) {
      _serviceChecked = true;
      _sessions[id] = session.copyWith(status: VpnStatus.failed);
      final message = _errorMessage(error);
      _errors[id] = message;
      if (_looksLikeServiceError(message)) _serviceError = message;
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
      } else {
        final current = _sessions[id] ?? session;
        final updated = _recordConnectionTransition(
          current,
          current.copyWith(status: VpnStatus.disconnected),
        );
        _sessions[id] = updated;
        if (_activityChanged(current, updated)) unawaited(_saveProfiles());
        _errors.remove(id);
        notifyListeners();
      }
    } on Object catch (error) {
      _serviceChecked = true;
      _sessions[id] = session.copyWith(status: VpnStatus.failed);
      final message = _errorMessage(error);
      _errors[id] = message;
      if (_looksLikeServiceError(message)) _serviceError = message;
      notifyListeners();
    }
  }

  Future<void> loadSnapshot() async {
    final engineClient = _engineClient;
    if (engineClient == null) {
      _serviceChecked = true;
      _serviceError = 'OpenVPN 服务未接入';
      notifyListeners();
      return;
    }
    _serviceChecked = false;
    _serviceError = null;
    notifyListeners();
    try {
      final result = await engineClient.snapshot();
      if (!result.ok) {
        _serviceChecked = true;
        _serviceError =
            result.message.isEmpty ? 'OpenVPN 服务不可用' : result.message;
        notifyListeners();
        return;
      }
      _serviceChecked = true;
      _serviceError = null;
      for (final remote in result.sessions) {
        final local = _sessions[remote.id];
        _sessions[remote.id] =
            local == null ? remote : _mergeRemote(local, remote);
      }
      notifyListeners();
    } on Object catch (error) {
      _serviceChecked = true;
      _serviceError = _errorMessage(error);
      notifyListeners();
    }
  }

  String _errorMessage(Object error) {
    if (error is EngineClientException) return error.message;
    return 'OpenVPN 服务调用失败：$error';
  }

  bool _looksLikeServiceError(String message) =>
      message.contains('OpenVPN 服务') ||
      message.contains('daemon') ||
      message.contains('socket');

  bool _keepSessionName(String value) =>
      value.trim().isNotEmpty &&
      !RegExp(r'^profile-[0-9a-f]{8,}(?:\(\d+\))?$', caseSensitive: false)
          .hasMatch(value.trim());

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

  VpnSession _recordConnectionTransition(
    VpnSession previous,
    VpnSession next,
  ) {
    final now = DateTime.now();
    if (next.status == VpnStatus.connected &&
        previous.status != VpnStatus.connected) {
      return next.copyWith(
        connectionCount: previous.connectionCount + 1,
        lastConnectedAt: now,
      );
    }
    if (next.status == VpnStatus.disconnected &&
        previous.status != VpnStatus.disconnected) {
      return next.copyWith(lastDisconnectedAt: now);
    }
    return next;
  }

  bool _activityChanged(VpnSession previous, VpnSession next) =>
      previous.connectionCount != next.connectionCount ||
      previous.lastConnectedAt != next.lastConnectedAt ||
      previous.lastDisconnectedAt != next.lastDisconnectedAt;

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
