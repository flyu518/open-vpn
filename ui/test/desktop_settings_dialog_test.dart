import 'dart:io';

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

  testWidgets('保存开机自启后通知托盘同步复选状态', (tester) async {
    var startupChanged = false;
    await tester.pumpWidget(
      MaterialApp(
        home: DesktopSettingsDialog(
          startupService: StartupService(
            platform: DesktopPlatform.windows,
            executablePath: r'C:\Program Files\OpenVPN\OpenVPN.exe',
            commandRunner: (executable, arguments) async {
              if (arguments.first == 'query') {
                return ProcessResult(1, 1, '', 'not found');
              }
              return ProcessResult(1, 0, '', '');
            },
          ),
          onStartupChanged: (_) => startupChanged = true,
        ),
      ),
    );
    await tester.pumpAndSettle();

    await tester.tap(find.byType(SwitchListTile));
    await tester.pumpAndSettle();

    expect(startupChanged, isTrue);
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

  testWidgets('没有当前平台安装包时按钮明确打开 Release 页面', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: OpenVpnAboutDialog(
          version: '0.1.1',
          updateService: UpdateService(
            apiBaseUrl: 'https://gitlab.example.com/api/v4',
            projectPath: 'group/open-vpn',
            currentVersion: '0.1.1',
            responseLoader: (_) async => const UpdateHttpResponse(
              200,
              '{"tag_name":"v0.2.0","name":"OpenVPN 0.2.0"}',
            ),
          ),
          platform: UpdatePlatform.macos,
          onOpenReleasePage: (_) async {},
        ),
      ),
    );

    await tester.tap(find.text('检查更新'));
    await tester.pumpAndSettle();

    expect(find.text('打开 Release 页面'), findsOneWidget);
    expect(find.text('下载并安装'), findsNothing);
  });

  testWidgets('打开 Release 页面失败时显示可读错误而不是抛出', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: OpenVpnAboutDialog(
          version: '0.1.1',
          updateService: UpdateService(
            apiBaseUrl: 'https://gitlab.example.com/api/v4',
            projectPath: 'group/open-vpn',
            currentVersion: '0.1.1',
            responseLoader: (_) async => const UpdateHttpResponse(
              200,
              '{"tag_name":"v0.2.0","name":"OpenVPN 0.2.0"}',
            ),
          ),
          platform: UpdatePlatform.macos,
          onOpenReleasePage: (_) async => throw StateError('浏览器不可用'),
        ),
      ),
    );

    await tester.tap(find.text('检查更新'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('打开 Release 页面'));
    await tester.pumpAndSettle();

    expect(find.textContaining('下载或打开安装包失败'), findsOneWidget);
  });
}
