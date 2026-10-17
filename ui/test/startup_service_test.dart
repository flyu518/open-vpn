import 'package:flutter_test/flutter_test.dart';

import 'package:openvpn_ui/services/startup_service.dart';

void main() {
  test('macOS LaunchAgent plist 保留带空格的应用路径', () {
    final service = StartupService(
      platform: DesktopPlatform.macos,
      executablePath: '/Applications/Open VPN.app/Contents/MacOS/OpenVPN',
      homeDirectory: '/tmp/Open VPN Home',
    );

    expect(
      service.launchAgentPath,
      '/tmp/Open VPN Home/Library/LaunchAgents/com.openvpn.ui.plist',
    );
    expect(service.macOsPlist, contains('<string>/Applications/Open VPN.app'));
    expect(service.macOsPlist, contains('Contents/MacOS/OpenVPN'));
    expect(service.macOsPlist, contains('com.openvpn.ui'));
  });

  test('Windows Run 注册表命令把含空格路径作为一个值传递', () {
    final service = StartupService(
      platform: DesktopPlatform.windows,
      executablePath: r'C:\Program Files\OpenVPN\OpenVPN.exe',
    );

    expect(
      service.windowsEnableArguments,
      containsAll(<String>[
        'add',
        r'HKCU\Software\Microsoft\Windows\CurrentVersion\Run',
        '/v',
        'OpenVPN',
        '/d',
        r'"C:\Program Files\OpenVPN\OpenVPN.exe"',
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
}
