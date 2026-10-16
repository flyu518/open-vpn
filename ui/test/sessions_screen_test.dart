import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:openvpn_ui/main.dart';
import 'package:openvpn_ui/models/session.dart';
import 'package:openvpn_ui/screens/sessions_screen.dart';
import 'package:openvpn_ui/view_models/session_view_model.dart';

void main() {
  testWidgets('服务尚未检查时不会显示为已就绪', (tester) async {
    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(viewModel: SessionViewModel()),
    ));

    expect(find.text('服务已就绪'), findsNothing);
    expect(find.text('服务未连接'), findsNothing);
  });

  testWidgets('断开过程中不会再次显示连接按钮', (tester) async {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
        id: 'office-a',
        name: '研发网络',
        status: VpnStatus.disconnecting,
        profile: 'client\n',
      ),
    ]);

    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(viewModel: viewModel),
    ));

    expect(find.text('断开中…'), findsOneWidget);
    expect(find.text('连接'), findsNothing);
  });

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

    expect(find.text('连接列表'), findsOneWidget);
    expect(find.text('活动连接'), findsOneWidget);
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

    expect(find.text('需重新导入'), findsOneWidget);
  });

  testWidgets('总览页提供唯一明确的导入入口', (tester) async {
    final viewModel = SessionViewModel();

    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(
        viewModel: viewModel,
        onImportProfile: () {},
      ),
    ));

    expect(find.byTooltip('导入 OpenVPN 配置'), findsOneWidget);
    expect(find.text('导入第一个配置'), findsNothing);
  });

  testWidgets('连接卡片提供查看详情入口', (tester) async {
    final viewModel = SessionViewModel();
    viewModel.replaceSessions([
      const VpnSession(
        id: 'office-a',
        name: '研发网络',
        status: VpnStatus.disconnected,
        profile: 'client\n',
      ),
    ]);

    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(
        viewModel: viewModel,
        onViewDetails: (_) async {},
      ),
    ));

    expect(find.text('查看详情'), findsOneWidget);
  });

  testWidgets('左侧不显示未使用的配置文件菜单和平台标识', (tester) async {
    await tester.pumpWidget(const VpnApp());
    await tester.pumpAndSettle();

    expect(find.text('配置文件'), findsNothing);
    expect(find.text('安全模式'), findsNothing);
    expect(find.text('OpenVPN · Windows / macOS'), findsNothing);
  });

  testWidgets('总览不再渲染没有操作价值的侧栏品牌壳', (tester) async {
    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(viewModel: SessionViewModel()),
    ));

    expect(find.text('多连接控制台'), findsNothing);
    expect(find.text('工作区'), findsNothing);
    expect(find.text('连接总览'), findsOneWidget);
  });
}
