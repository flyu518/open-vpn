import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:openvpn_ui/models/session.dart';
import 'package:openvpn_ui/services/profile_store.dart';

void main() {
  test('持久化 profile 元数据但不写入 profile 内容', () async {
    final root = Directory.systemTemp.createTempSync('openvpn-profile-');
    final profileFile = File('${root.path}/研发网络.ovpn')
      ..writeAsStringSync('client\n<private-key>不要写入存储</private-key>\n');
    final store = JsonProfileStore(file: File('${root.path}/profiles.json'));

    await store.save([
      VpnSession(
        id: 'profile-a',
        name: '研发专线',
        status: VpnStatus.disconnected,
        profileId: 'profile-a',
        profilePath: profileFile.path,
        profile: 'client\n<private-key>不要写入存储</private-key>\n',
      ),
    ]);

    final serialized = await store.file.readAsString();
    expect(serialized, contains('研发专线'));
    expect(serialized, contains(profileFile.path));
    expect(serialized, isNot(contains('不要写入存储')));

    final restored = await store.load();
    expect(restored.single.name, '研发专线');
    expect(restored.single.profilePath, profileFile.path);
    expect(restored.single.profile, contains('client'));

    await root.delete(recursive: true);
  });
}
