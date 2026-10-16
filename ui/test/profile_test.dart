import 'package:flutter_test/flutter_test.dart';
import 'package:openvpn_ui/models/profile.dart';

void main() {
  test('profile id 对同一路径稳定且不包含路径分隔符', () {
    final first = ImportedProfile.fromFile(
      path: '/Users/test/研发网络.ovpn',
      contents: 'client\n',
    );
    final second = ImportedProfile.fromFile(
      path: '/Users/test/研发网络.ovpn',
      contents: 'client\n',
    );

    expect(first.id, second.id);
    expect(first.id, startsWith('profile-'));
    expect(first.id, isNot(contains('/')));
    expect(first.displayName, '研发网络');
  });

  test('从内嵌 auth-user-pass 中读取用户名但不暴露密码', () {
    final profile = ImportedProfile.fromFile(
      path: '/Users/test/研发网络.ovpn',
      contents: '''client
<auth-user-pass>
cuikai
不要保存这行密码
</auth-user-pass>
''',
    );

    expect(profile.username, 'cuikai');
    expect(profile.username, isNot(contains('不要保存')));
  });
}
