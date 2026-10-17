import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:openvpn_ui/services/startup_service.dart';

void main() {
  test('macOS LaunchAgent plist 保留带空格的应用路径', () {
    final service = StartupService(
      platform: DesktopPlatform.macos,
      executablePath: '/Applications/Open VPN.app/Contents/MacOS/OpenVPN',
      homeDirectory: '/tmp/用户 Open VPN Home',
    );

    expect(
      service.launchAgentPath,
      '/tmp/用户 Open VPN Home/Library/LaunchAgents/com.openvpn.ui.plist',
    );
    expect(service.macOsPlist, contains('<string>/Applications/Open VPN.app'));
    expect(service.macOsPlist, contains('Contents/MacOS/OpenVPN'));
    expect(service.macOsPlist, contains('com.openvpn.ui'));
  });

  test('Windows Run 注册表命令把含空格路径作为一个值传递', () {
    final service = StartupService(
      platform: DesktopPlatform.windows,
      executablePath: r'C:\Program Files\OpenVPN\用户\OpenVPN.exe',
    );

    expect(
      service.windowsEnableArguments,
      containsAll(<String>[
        'add',
        r'HKCU\Software\Microsoft\Windows\CurrentVersion\Run',
        '/v',
        'OpenVPN',
        '/d',
        r'"C:\Program Files\OpenVPN\用户\OpenVPN.exe"',
      ]),
    );
  });

  test('不支持的平台读取自启状态时返回 false', () async {
    final service = StartupService(
      platform: DesktopPlatform.unsupported,
      executablePath: '/tmp/openvpn',
    );

    expect(await service.isEnabled(), isFalse);
  });

  test('禁用自启时只忽略目标不存在，其他命令错误必须返回失败', () async {
    final service = StartupService(
      platform: DesktopPlatform.windows,
      executablePath: r'C:\Program Files\OpenVPN\OpenVPN.exe',
      commandRunner: (_, __) async => ProcessResult(1, 1, '', '拒绝访问'),
    );

    await expectLater(service.disable(), throwsA(isA<StartupException>()));
  });

  test('Windows 删除不存在的 Run 项是幂等操作', () async {
    final service = StartupService(
      platform: DesktopPlatform.windows,
      executablePath: r'C:\Program Files\OpenVPN\OpenVPN.exe',
      commandRunner: (_, __) async => ProcessResult(
        1,
        1,
        '',
        'ERROR: The system was unable to find the specified registry key or value.',
      ),
    );

    await service.disable();
  });

  test('同一用户的 OpenVPN 实例锁不能被第二个进程同时持有', () {
    final directory = Directory.systemTemp.createTempSync('openvpn-lock-test-');
    final lockPath = '${directory.path}/openvpn-ui.lock';
    final first = SingleInstanceGuard(lockPath: lockPath);
    final second = SingleInstanceGuard(lockPath: lockPath);

    expect(first.acquire(), isTrue);
    expect(second.acquire(), isFalse);
    first.release();
    expect(second.acquire(), isTrue);

    second.release();
    directory.deleteSync(recursive: true);
  });
}
