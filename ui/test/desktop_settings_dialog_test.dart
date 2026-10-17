import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:openvpn_ui/screens/about_dialog.dart';
import 'package:openvpn_ui/screens/desktop_settings_dialog.dart';
import 'package:openvpn_ui/services/startup_service.dart';
import 'package:openvpn_ui/services/update_service.dart';

void main() {
  testWidgets('设置对话框只展示有实际作用的桌面设置', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: DesktopSettingsDialog(
          startupService: StartupService(
            platform: DesktopPlatform.unsupported,
            executablePath: '/tmp/openvpn',
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.text('桌面设置'), findsOneWidget);
    expect(find.text('开机自启'), findsOneWidget);
    expect(find.text('检查更新'), findsOneWidget);
    expect(find.text('安全模式'), findsNothing);
  });

  testWidgets('关于对话框显示版本和更新入口', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: OpenVpnAboutDialog(
          version: '0.1.0',
          updateService: UpdateService(
            apiBaseUrl: '',
            projectPath: '',
            currentVersion: '0.1.0',
          ),
          platform: UpdatePlatform.macos,
        ),
      ),
    );

    expect(find.text('关于 OpenVPN'), findsOneWidget);
    expect(find.text('当前版本 0.1.0'), findsOneWidget);
    expect(find.text('检查更新'), findsOneWidget);
  });
}
