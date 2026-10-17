import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:openvpn_ui/models/credentials.dart';
import 'package:openvpn_ui/screens/credentials_dialog.dart';

void main() {
  testWidgets('profile 用户名会预填到连接弹窗', (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: CredentialsDialog(
        profileName: '研发网络',
        initialUsername: 'cuikai',
      ),
    ));

    expect(find.text('cuikai'), findsOneWidget);
    expect(find.text('已从配置文件读取，可按需修改'), findsOneWidget);
  });

  testWidgets('凭据弹窗只负责首次输入或更新密码', (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: CredentialsDialog(
        profileName: '研发网络',
        initialUsername: 'cuikai',
      ),
    ));

    expect(find.byType(TextFormField), findsNWidgets(2));
    expect(find.text('使用已保存的凭据'), findsNothing);
    expect(find.text('保存到系统安全存储'), findsOneWidget);
  });

  test('只把缺少凭据或认证失败识别为需要重新输入', () {
    expect(
      isCredentialFailureMessage('Keychain 中没有对应凭据'),
      isTrue,
    );
    expect(isCredentialFailureMessage('AUTH_FAILED: 密码错误'), isTrue);
    expect(isCredentialFailureMessage('路由与活动 VPN 重叠'), isFalse);
  });
}
