import 'package:flutter/foundation.dart';

import '../models/session.dart';
import '../models/route_strategy.dart';

class SessionViewModel extends ChangeNotifier {
  final Map<String, VpnSession> _sessions = <String, VpnSession>{};
  final Map<String, String> _errors = <String, String>{};

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

  void updateStatus(String id, VpnStatus status) {
    final session = _sessions[id];
    if (session == null) return;
    _sessions[id] = session.copyWith(status: status);
    notifyListeners();
  }

  void applyError(String id, String message) {
    _errors[id] = message;
    notifyListeners();
  }

  void clearError(String id) {
    if (_errors.remove(id) != null) notifyListeners();
  }

  void updateStrategy(String id, RouteStrategy strategy) {
    final session = _sessions[id];
    if (session == null) return;
    _sessions[id] = session.copyWith(routeStrategy: strategy);
    notifyListeners();
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
  }
}
