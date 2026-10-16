import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
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

  testWidgets('使用已保存凭据时不再要求输入密码', (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: CredentialsDialog(
        profileName: '研发网络',
        initialUsername: 'cuikai',
      ),
    ));

    expect(find.byType(TextFormField), findsNWidgets(2));
    await tester.tap(find.text('使用已保存的凭据'));
    await tester.pump();

    expect(find.byType(TextFormField), findsOneWidget);
    expect(find.text('再次输入密码'), findsNothing);
  });
}
