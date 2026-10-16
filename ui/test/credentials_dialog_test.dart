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
  });
}
