import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:openvpn_ui/models/session.dart';
import 'package:openvpn_ui/screens/profile_details_dialog.dart';

void main() {
  testWidgets('详情只展示可读配置并隐藏敏感内容', (tester) async {
    const session = VpnSession(
      id: 'profile-a',
      name: '研发网络',
      status: VpnStatus.disconnected,
      profile: '''client
remote vpn.example.com 1194 udp
auth-user-pass
<private-key>
PRIVATE-SECRET
</private-key>
''',
      username: 'cuikai',
      profilePath: '/tmp/profile-a.ovpn',
    );

    await tester.pumpWidget(const MaterialApp(
      home: ProfileDetailsDialog(session: session),
    ));

    expect(find.textContaining('vpn.example.com 1194 udp'), findsOneWidget);
    expect(find.text('PRIVATE-SECRET'), findsNothing);
    expect(find.textContaining('私钥内容已隐藏'), findsOneWidget);
    expect(find.text('导出配置'), findsOneWidget);
    expect(find.text('重新导入'), findsOneWidget);
  });
}
