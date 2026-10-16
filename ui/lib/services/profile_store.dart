import 'dart:convert';
import 'dart:io';

import '../models/route_strategy.dart';
import '../models/session.dart';
import '../models/profile.dart';

abstract class ProfileStore {
  Future<List<VpnSession>> load();

  Future<void> save(Iterable<VpnSession> sessions);
}

class JsonProfileStore implements ProfileStore {
  JsonProfileStore({File? file}) : file = file ?? _defaultFile();

  final File file;

  @override
  Future<List<VpnSession>> load() async {
    if (!await file.exists()) return <VpnSession>[];
    try {
      final value = jsonDecode(await file.readAsString());
      if (value is! Map) return <VpnSession>[];
      final rawProfiles = value['profiles'];
      if (rawProfiles is! List) return <VpnSession>[];

      final sessions = <VpnSession>[];
      for (final raw in rawProfiles.whereType<Map>()) {
        final item = Map<String, dynamic>.from(raw);
        final path = item['path'] as String? ?? '';
        final id = item['id'] as String? ?? '';
        final name = item['name'] as String? ?? '未命名配置';
        if (id.isEmpty || path.isEmpty) continue;

        var contents = '';
        var username = '';
        final profileFile = File(path);
        if (await profileFile.exists()) {
          contents = await profileFile.readAsString();
          username = ImportedProfile.fromFile(
            path: path,
            contents: contents,
          ).username;
        }
        final strategyValue = item['routeStrategy'];
        final strategy = strategyValue is Map
            ? RouteStrategy.fromJson(Map<String, dynamic>.from(strategyValue))
            : const RouteStrategy();
        sessions.add(VpnSession(
          id: id,
          name: name,
          status: VpnStatus.disconnected,
          profileId: id,
          profile: contents,
          profilePath: path,
          username: username,
          routeStrategy: strategy,
        ));
      }
      return sessions;
    } on Object {
      return <VpnSession>[];
    }
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
            })
        .toList(growable: false);
    final payload =
        const JsonEncoder.withIndent('  ').convert(<String, Object?>{
      'version': 1,
      'profiles': profiles,
    });
    await file.parent.create(recursive: true);
    await file.writeAsString('$payload\n');
  }
}

File _defaultFile() {
  final environment = Platform.environment;
  final home = environment['HOME'] ??
      environment['USERPROFILE'] ??
      Directory.current.path;
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

String _join(String first,
    [String second = '', String third = '', String fourth = '']) {
  final separator = Platform.isWindows ? '\\' : '/';
  return <String>[first, second, third, fourth]
      .where((part) => part.isNotEmpty)
      .join(separator);
}
