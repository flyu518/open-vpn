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

    expect(find.text('检查中'), findsOneWidget);
    expect(find.text('服务已就绪'), findsNothing);
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

    expect(find.text('导入配置'), findsOneWidget);
    expect(find.text('导入第一个配置'), findsNothing);
  });

  testWidgets('配置文件导航进入配置管理而不是直接导入', (tester) async {
    var opened = false;
    final viewModel = SessionViewModel();

    await tester.pumpWidget(MaterialApp(
      home: SessionsScreen(
        viewModel: viewModel,
        onOpenProfiles: () => opened = true,
      ),
    ));

    await tester.tap(find.text('配置文件'));
    await tester.pump();

    expect(opened, isTrue);
  });

  testWidgets('应用根页面点击配置文件会打开配置管理', (tester) async {
    await tester.pumpWidget(const VpnApp());
    await tester.pumpAndSettle();

    await tester.tap(find.text('配置文件').first);
    await tester.pumpAndSettle();

    expect(find.text('配置文件'), findsWidgets);
    expect(find.text('添加配置'), findsOneWidget);
  });
}
