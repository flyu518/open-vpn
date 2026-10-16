import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:vpn_ui/models/session.dart';
import 'package:vpn_ui/screens/sessions_screen.dart';
import 'package:vpn_ui/view_models/session_view_model.dart';

void main() {
  testWidgets('路由冲突失败时显示先断开前一个会话的处理提示', (tester) async {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
        id: 'office-b',
        name: '财务网络',
        status: VpnStatus.failed,
      ),
    ]);
    viewModel.applyError('office-b', '路由与活动 VPN 重叠：前一个会话 office-a');

    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(viewModel: viewModel),
    ));

    expect(find.textContaining('先断开提示中的前一个会话，再重试'), findsOneWidget);
  });

  testWidgets('恢复的会话没有本地 profile 时提示重新导入', (tester) async {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
        id: 'office-a',
        name: '研发网络',
        status: VpnStatus.disconnected,
      ),
    ]);

    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(viewModel: viewModel),
    ));

    expect(find.text('需要重新导入'), findsOneWidget);
  });
}
