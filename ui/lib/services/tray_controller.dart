// tray_manager 0.8 保留了兼容 API，Windows/macOS 两端的 Flutter 运行时都能使用；
// 这里集中屏蔽弃用提示，后续 native API 稳定后只需要替换本文件。
// ignore_for_file: deprecated_member_use

import 'dart:async';
import 'dart:io';

import 'package:tray_manager/legacy.dart';

import '../models/session.dart';
import '../view_models/session_view_model.dart';
import 'startup_service.dart';

enum TrayMenuAction {
  open,
  status,
  connect,
  disconnect,
  startup,
  settings,
  checkUpdates,
  about,
  quit,
  separator,
}

class TrayMenuEntry {
  const TrayMenuEntry({
    required this.label,
    required this.action,
    this.sessionId,
    this.enabled = true,
    this.checked,
  });

  final String label;
  final TrayMenuAction action;
  final String? sessionId;
  final bool enabled;
  final bool? checked;
}

class TrayMenuProjection {
  const TrayMenuProjection._();

  static bool isSupported(DesktopPlatform platform) =>
      platform == DesktopPlatform.macos || platform == DesktopPlatform.windows;

  static List<TrayMenuEntry> build({
    required String serviceLabel,
    required bool startupEnabled,
    required List<VpnSession> sessions,
  }) {
    final activeCount = sessions
        .where((session) => session.status == VpnStatus.connected)
        .length;
    final entries = <TrayMenuEntry>[
      const TrayMenuEntry(label: '打开 OpenVPN', action: TrayMenuAction.open),
      TrayMenuEntry(
        label: '$serviceLabel · 活动连接 $activeCount',
        action: TrayMenuAction.status,
        enabled: false,
      ),
    ];

    if (sessions.isNotEmpty) {
      entries.add(const TrayMenuEntry(
        label: '',
        action: TrayMenuAction.separator,
      ));
      for (final session in sessions) {
        entries.add(_sessionEntry(session));
      }
    }

    entries.addAll(<TrayMenuEntry>[
      const TrayMenuEntry(label: '', action: TrayMenuAction.separator),
      TrayMenuEntry(
        label: '开机自启',
        action: TrayMenuAction.startup,
        checked: startupEnabled,
      ),
      const TrayMenuEntry(label: '设置', action: TrayMenuAction.settings),
      const TrayMenuEntry(label: '检查更新', action: TrayMenuAction.checkUpdates),
      const TrayMenuEntry(label: '关于 OpenVPN', action: TrayMenuAction.about),
      const TrayMenuEntry(label: '', action: TrayMenuAction.separator),
      const TrayMenuEntry(label: '退出 OpenVPN', action: TrayMenuAction.quit),
    ]);
    return List<TrayMenuEntry>.unmodifiable(entries);
  }

  static TrayMenuEntry _sessionEntry(VpnSession session) {
    final name = session.name.trim().isEmpty ? session.id : session.name;
    return switch (session.status) {
      VpnStatus.connected => TrayMenuEntry(
          label:
              '断开：$name${_remoteHostSuffix(session)} · ${formatDuration(session.duration)} · ↓${formatBytes(session.downloadBytes)} ↑${formatBytes(session.uploadBytes)}',
          action: TrayMenuAction.disconnect,
          sessionId: session.id,
        ),
      VpnStatus.connecting => TrayMenuEntry(
          label: '连接中：$name',
          action: TrayMenuAction.status,
          sessionId: session.id,
          enabled: false,
        ),
      VpnStatus.disconnecting => TrayMenuEntry(
          label: '断开中：$name',
          action: TrayMenuAction.status,
          sessionId: session.id,
          enabled: false,
        ),
      VpnStatus.failed => TrayMenuEntry(
          label: '重试连接：$name',
          action: TrayMenuAction.connect,
          sessionId: session.id,
        ),
      VpnStatus.disconnected => TrayMenuEntry(
          label: '连接：$name',
          action: TrayMenuAction.connect,
          sessionId: session.id,
        ),
    };
  }

  static String _remoteHostSuffix(VpnSession session) {
    final host = session.remoteHost.trim();
    return host.isEmpty ? '' : ' · $host';
  }
}

String formatDuration(Duration duration) {
  final hours = duration.inHours;
  final minutes = duration.inMinutes.remainder(60).toString().padLeft(2, '0');
  final seconds = duration.inSeconds.remainder(60).toString().padLeft(2, '0');
  return hours > 0
      ? '${hours.toString().padLeft(2, '0')}:$minutes:$seconds'
      : '$minutes:$seconds';
}

String formatBytes(int bytes) {
  if (bytes < 1024) return '$bytes B';
  const units = <String>['KB', 'MB', 'GB', 'TB'];
  var value = bytes.toDouble();
  var unitIndex = -1;
  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex++;
  }
  final display = value >= 10 ? value.toStringAsFixed(0) : value.toStringAsFixed(1);
  return '$display ${units[unitIndex]}';
}

class TrayController with TrayListener {
  TrayController({
    required SessionViewModel viewModel,
    required Future<void> Function() onOpen,
    required Future<void> Function(VpnSession session) onConnect,
    required Future<void> Function(VpnSession session) onDisconnect,
    required Future<void> Function() onSettings,
    required Future<void> Function() onCheckUpdates,
    required Future<void> Function() onAbout,
    required Future<void> Function() onQuit,
    StartupService? startupService,
    DesktopPlatform? platform,
    void Function(String message)? onDiagnostic,
    Future<void> Function()? contextMenuOpener,
  })  : _viewModel = viewModel,
        _onOpen = onOpen,
        _onConnect = onConnect,
        _onDisconnect = onDisconnect,
        _onSettings = onSettings,
        _onCheckUpdates = onCheckUpdates,
        _onAbout = onAbout,
        _onQuit = onQuit,
        _startupService = startupService ?? StartupService(),
        _platform = platform ?? _detectPlatform(),
        _onDiagnostic = onDiagnostic,
        _contextMenuOpener = contextMenuOpener;

  final SessionViewModel _viewModel;
  final Future<void> Function() _onOpen;
  final Future<void> Function(VpnSession session) _onConnect;
  final Future<void> Function(VpnSession session) _onDisconnect;
  final Future<void> Function() _onSettings;
  final Future<void> Function() _onCheckUpdates;
  final Future<void> Function() _onAbout;
  final Future<void> Function() _onQuit;
  final StartupService _startupService;
  final DesktopPlatform _platform;
  final void Function(String message)? _onDiagnostic;
  final Future<void> Function()? _contextMenuOpener;
  bool _initialized = false;
  bool _disposed = false;
  bool _startupEnabled = false;
  bool _refreshInFlight = false;
  bool _refreshAgain = false;

  bool get isInitialized => _initialized;

  Future<void> setStartupEnabled(bool enabled) async {
    _startupEnabled = enabled;
    await refresh();
  }

  Future<bool> initialize() async {
    if (_disposed || !TrayMenuProjection.isSupported(_platform)) return false;
    var listenerAdded = false;
    var viewModelListenerAdded = false;
    try {
      if (!trayManager.isSupported) return false;
      trayManager.addListener(this);
      listenerAdded = true;
      await trayManager.setIcon('assets/openvpn_tray.png');
      await trayManager.setToolTip('OpenVPN');
      if (Platform.isMacOS) await trayManager.setTitle('');
      _startupEnabled = await _startupService.isEnabled();
      _initialized = true;
      _viewModel.addListener(_onViewModelChanged);
      viewModelListenerAdded = true;
      await refresh();
      return true;
    } on Object catch (error) {
      _onDiagnostic?.call('状态栏/托盘初始化失败：$error');
      _initialized = false;
      if (viewModelListenerAdded) _viewModel.removeListener(_onViewModelChanged);
      if (listenerAdded) trayManager.removeListener(this);
      try {
        await trayManager.destroy();
      } on Object {
        // 清理失败不应阻止主窗口继续使用。
      }
      return false;
    }
  }

  Future<void> refresh() async {
    if (_disposed || !_initialized) return;
    if (_refreshInFlight) {
      _refreshAgain = true;
      return;
    }
    _refreshInFlight = true;
    try {
      final entries = TrayMenuProjection.build(
        serviceLabel: _serviceLabel,
        startupEnabled: _startupEnabled,
        sessions: _viewModel.sessions,
      );
      await trayManager.setContextMenu(
        Menu(items: entries.map(_nativeMenuItem).toList(growable: false)),
      );
    } on Object catch (error) {
      _onDiagnostic?.call('刷新状态栏菜单失败：$error');
    } finally {
      _refreshInFlight = false;
      if (_refreshAgain && !_disposed) {
        _refreshAgain = false;
        unawaited(refresh());
      }
    }
  }

  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    _viewModel.removeListener(_onViewModelChanged);
    if (_initialized) {
      await trayManager.destroy();
      _initialized = false;
    }
  }

  @override
  void onTrayIconMouseUp() {
    unawaited(_onOpen());
  }

  @override
  void onTrayIconRightMouseUp() {
    unawaited((_contextMenuOpener ?? trayManager.popUpContextMenu)());
  }

  MenuItem _nativeMenuItem(TrayMenuEntry entry) {
    if (entry.action == TrayMenuAction.separator) return MenuItem.separator();
    if (entry.action == TrayMenuAction.startup) {
      return MenuItem.checkbox(
        key: 'startup',
        label: entry.label,
        checked: entry.checked ?? false,
        disabled: !entry.enabled,
        onClick: (_) => unawaited(_toggleStartup()),
      );
    }
    return MenuItem(
      key: _menuKey(entry),
      label: entry.label,
      disabled: !entry.enabled,
      onClick: (_) => unawaited(_handleEntry(entry)),
    );
  }

  Future<void> _handleEntry(TrayMenuEntry entry) async {
    switch (entry.action) {
      case TrayMenuAction.open:
        await _onOpen();
      case TrayMenuAction.connect:
        final session = _session(entry.sessionId);
        if (session != null) await _onConnect(session);
      case TrayMenuAction.disconnect:
        final session = _session(entry.sessionId);
        if (session != null) await _onDisconnect(session);
      case TrayMenuAction.settings:
        await _onSettings();
      case TrayMenuAction.checkUpdates:
        await _onCheckUpdates();
      case TrayMenuAction.about:
        await _onAbout();
      case TrayMenuAction.quit:
        await _onQuit();
      case TrayMenuAction.status || TrayMenuAction.startup || TrayMenuAction.separator:
        return;
    }
  }

  Future<void> _toggleStartup() async {
    final next = !_startupEnabled;
    try {
      if (next) {
        await _startupService.enable();
      } else {
        await _startupService.disable();
      }
      _startupEnabled = next;
      await refresh();
    } on Object catch (error) {
      _onDiagnostic?.call('保存开机自启失败：$error');
    }
  }

  void _onViewModelChanged() {
    unawaited(refresh());
  }

  VpnSession? _session(String? id) => id == null ? null : _viewModel.sessionById(id);

  String get _serviceLabel {
    final error = _viewModel.serviceError;
    if (error != null && error.isNotEmpty) return '服务异常';
    if (!_viewModel.serviceChecked) return '服务检查中';
    return '服务已就绪';
  }

  String _menuKey(TrayMenuEntry entry) {
    if (entry.sessionId != null) return '${entry.action.name}:${entry.sessionId}';
    return entry.action.name;
  }

  static DesktopPlatform _detectPlatform() {
    if (Platform.isMacOS) return DesktopPlatform.macos;
    if (Platform.isWindows) return DesktopPlatform.windows;
    return DesktopPlatform.unsupported;
  }
}
