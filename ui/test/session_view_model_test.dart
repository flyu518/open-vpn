import 'package:flutter_test/flutter_test.dart';
import 'package:vpn_ui/models/session.dart';
import 'package:vpn_ui/view_models/session_view_model.dart';

void main() {
  test('桌面端两个 VPN 会话显示为互相独立的卡片', () {
    final viewModel = SessionViewModel();

    viewModel.replaceSessions([
      VpnSession(id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      VpnSession(id: 'office-b', name: '财务网络', status: VpnStatus.connecting),
    ]);

    expect(viewModel.sessions.map((session) => session.id),
        containsAll(<String>['office-a', 'office-b']));
    expect(viewModel.activeLabel, '1 个连接活动中');
  });

  test('路由冲突只提示错误，不移除已有连接', () {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      VpnSession(id: 'office-a', name: '研发网络', status: VpnStatus.connected),
    ]);

    viewModel.applyError('office-b', '路由与活动 VPN 重叠');

    expect(viewModel.sessions.single.id, 'office-a');
    expect(viewModel.errorFor('office-b'), '路由与活动 VPN 重叠');
  });

  test('事件只更新对应会话', () {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      VpnSession(id: 'office-a', name: '研发网络', status: VpnStatus.connected),
      VpnSession(id: 'office-b', name: '财务网络', status: VpnStatus.connecting),
    ]);

    viewModel.updateStatus('office-b', VpnStatus.failed);

    expect(viewModel.sessionById('office-a')!.status, VpnStatus.connected);
    expect(viewModel.sessionById('office-b')!.status, VpnStatus.failed);
  });
}
