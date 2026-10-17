import 'dart:convert';
import 'dart:io';

import '../models/route_strategy.dart';
import '../models/session.dart';
import '../models/profile.dart';

abstract class ProfileStore {
  String get location => 'OpenVPN 配置索引';

  Future<List<VpnSession>> load();

  Future<void> save(Iterable<VpnSession> sessions);
}

class JsonProfileStore implements ProfileStore {
  JsonProfileStore({File? file}) : file = file ?? _defaultFile();

  final File file;

  @override
  String get location => file.path;

  @override
  Future<List<VpnSession>> load() async {
    if (!await file.exists()) return <VpnSession>[];
    dynamic value;
    try {
      value = jsonDecode(await file.readAsString());
    } on Object {
      return <VpnSession>[];
    }
    if (value is! Map) return <VpnSession>[];
    final rawProfiles = value['profiles'];
    if (rawProfiles is! List) return <VpnSession>[];

    final sessions = <VpnSession>[];
    for (final raw in rawProfiles.whereType<Map>()) {
      try {
        final item = Map<String, dynamic>.from(raw);
        final path = item['path'] as String? ?? '';
        final id = item['id'] as String? ?? '';
        final name = item['name'] as String? ?? '未命名配置';
        if (id.isEmpty || path.isEmpty) continue;

        var contents = '';
        var username = '';
        var resolvedName = name;
        final profileFile = File(path);
        if (await profileFile.exists()) {
          contents = await profileFile.readAsString();
          final parsed = ImportedProfile.fromFile(
            path: path,
            contents: contents,
          );
          username = parsed.username;
          if (_isGeneratedName(name)) resolvedName = parsed.displayName;
        }
        final strategyValue = item['routeStrategy'];
        final strategy = strategyValue is Map
            ? RouteStrategy.fromJson(Map<String, dynamic>.from(strategyValue))
            : const RouteStrategy();
        sessions.add(VpnSession(
          id: id,
          name: resolvedName,
          status: VpnStatus.disconnected,
          profileId: id,
          profile: contents,
          profilePath: path,
          username: username,
          routeStrategy: strategy,
          connectionCount: _asNonNegativeInt(item['connectionCount']),
          lastConnectedAt: _parseDateTime(item['lastConnectedAt']),
          lastDisconnectedAt: _parseDateTime(item['lastDisconnectedAt']),
        ));
      } on Object {
        // 单个条目损坏时跳过它，保留其他仍可读取的配置。
        continue;
      }
    }
    return sessions;
  }

  @override
  Future<void> save(Iterable<VpnSession> sessions) async {
    final profiles = sessions
        .where((session) =>
            session.profileId.isNotEmpty && session.profilePath.isNotEmpty)
        .map((session) => <String, Object?>{
              'id': session.profileId,
              'name': session.name,
              'path': session.profilePath,
              'routeStrategy': session.routeStrategy.toJson(),
              'connectionCount': session.connectionCount,
              if (session.lastConnectedAt != null)
                'lastConnectedAt': session.lastConnectedAt!.toIso8601String(),
              if (session.lastDisconnectedAt != null)
                'lastDisconnectedAt':
                    session.lastDisconnectedAt!.toIso8601String(),
            })
        .toList(growable: false);
    final payload =
        const JsonEncoder.withIndent('  ').convert(<String, Object?>{
      'version': 1,
      'profiles': profiles,
    });
    await file.parent.create(recursive: true);
    final temporary = File('${file.path}.tmp');
    try {
      await temporary.writeAsString('$payload\n');
      try {
        await temporary.rename(file.path);
      } on FileSystemException {
        // macOS 某些文件系统/权限场景下 rename 可能失败，直接写入作为
        // 可恢复兜底，避免用户修改名称或策略后只能看到“保存失败”。
        await file.writeAsString('$payload\n');
      }
    } finally {
      if (await temporary.exists()) await temporary.delete();
    }
  }
}

int _asNonNegativeInt(Object? value) {
  final number = value is num ? value.toInt() : 0;
  return number < 0 ? 0 : number;
}

DateTime? _parseDateTime(Object? value) {
  if (value is! String || value.trim().isEmpty) return null;
  return DateTime.tryParse(value);
}

bool _isGeneratedName(String value) =>
    RegExp(r'^profile-[0-9a-f]{8,}(?:\(\d+\))?$', caseSensitive: false)
        .hasMatch(value);

File _defaultFile() {
  final environment = Platform.environment;
  final home = _homeDirectory(environment);
  final root = Platform.isMacOS
      ? _join(home, 'Library', 'Application Support', 'OpenVPN')
      : Platform.isWindows
          ? _join(environment['APPDATA'] ?? home, 'OpenVPN')
          : _join(
              environment['XDG_CONFIG_HOME'] ?? _join(home, '.config'),
              'OpenVPN',
            );
  return File(_join(root, 'profiles.json'));
}

String _homeDirectory(Map<String, String> environment) {
  final configured = environment['HOME'] ?? environment['USERPROFILE'];
  if (Platform.isMacOS) {
    // Finder、Installer、IDE 和提权启动的环境变量可能把 USER/HOME
    // 设成 root 或其他服务用户；macOS 当前登录用户以 id -un 为准。
    final detectedUser = _currentMacUser();
    final user = detectedUser.isNotEmpty
        ? detectedUser
        : environment['USER'] ?? environment['LOGNAME'] ?? '';
    // 从 Finder、launchd 或自动化工具启动 macOS 应用时，HOME 可能缺失或
    // 被设置为服务用户目录；优先使用当前登录用户的固定目录，保证升级后
    // 仍然读取同一份 OpenVPN 配置索引。
    if (user.isNotEmpty) {
      final macHome = _join('/Users', user);
      if (configured == null ||
          configured.isEmpty ||
          !configured.startsWith('/Users/')) {
        return macHome;
      }
    }
  }
  if (configured != null && configured.isNotEmpty) return configured;
  return Directory.current.path;
}

String _currentMacUser() {
  try {
    final result = Process.runSync('/usr/bin/id', <String>['-un']);
    return result.exitCode == 0 ? result.stdout.toString().trim() : '';
  } on Object {
    return '';
  }
}

String _join(String first,
    [String second = '', String third = '', String fourth = '']) {
  final separator = Platform.isWindows ? '\\' : '/';
  return <String>[first, second, third, fourth]
      .where((part) => part.isNotEmpty)
      .join(separator);
}
