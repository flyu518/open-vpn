import 'package:flutter_test/flutter_test.dart';

import 'package:openvpn_ui/models/session.dart';
import 'package:openvpn_ui/services/startup_service.dart';
import 'package:openvpn_ui/services/tray_controller.dart';
import 'package:openvpn_ui/view_models/session_view_model.dart';

void main() {
  test('托盘菜单投影使用最新连接状态和中文动作', () {
    final entries = TrayMenuProjection.build(
      serviceLabel: '服务已就绪',
      startupEnabled: true,
      sessions: const <VpnSession>[
        VpnSession(
          id: 'office',
          name: '办公网络',
          status: VpnStatus.connected,
          remoteHost: 'office.example.com',
          downloadBytes: 1024,
          uploadBytes: 2048,
          duration: Duration(seconds: 65),
        ),
        VpnSession(
          id: 'lab',
          name: '测试网络',
          status: VpnStatus.disconnected,
        ),
      ],
    );

    expect(entries.any((entry) => entry.label == '服务已就绪 · 活动连接 1'), isTrue);
    expect(
      entries.firstWhere((entry) => entry.sessionId == 'office').label,
      contains('断开：办公网络'),
    );
    expect(
      entries.firstWhere((entry) => entry.sessionId == 'office').label,
      contains('office.example.com'),
    );
    expect(
      entries.firstWhere((entry) => entry.sessionId == 'office').label,
      contains('↓1.0 KB'),
    );
    expect(
      entries.firstWhere((entry) => entry.sessionId == 'lab').label,
      contains('连接：测试网络'),
    );
    expect(
      entries.firstWhere((entry) => entry.action == TrayMenuAction.startup).checked,
      isTrue,
    );
  });

  test('非 macOS/Windows 平台不启用托盘', () {
    expect(TrayMenuProjection.isSupported(DesktopPlatform.unsupported), isFalse);
    expect(TrayMenuProjection.isSupported(DesktopPlatform.macos), isTrue);
    expect(TrayMenuProjection.isSupported(DesktopPlatform.windows), isTrue);
  });

  test('右键托盘图标会显式弹出上下文菜单', () async {
    var opened = false;
    final controller = TrayController(
      viewModel: SessionViewModel(),
      startupService: StartupService(
        platform: DesktopPlatform.unsupported,
        executablePath: '/tmp/openvpn',
      ),
      platform: DesktopPlatform.unsupported,
      onOpen: () async {},
      onConnect: (_) async {},
      onDisconnect: (_) async {},
      onSettings: () async {},
      onCheckUpdates: () async {},
      onAbout: () async {},
      onQuit: () async {},
      contextMenuOpener: () async {
        opened = true;
      },
    );

    controller.onTrayIconRightMouseUp();
    await Future<void>.delayed(Duration.zero);

    expect(opened, isTrue);
    await controller.dispose();
  });
}
