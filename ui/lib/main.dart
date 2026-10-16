import 'package:flutter/material.dart';

import 'models/session.dart';
import 'models/route_strategy.dart';
import 'screens/profile_import_screen.dart';
import 'screens/route_strategy_dialog.dart';
import 'screens/sessions_screen.dart';
import 'view_models/session_view_model.dart';

void main() {
  runApp(const VpnApp());
}

class VpnApp extends StatefulWidget {
  const VpnApp({super.key});

  @override
  State<VpnApp> createState() => _VpnAppState();
}

class _VpnAppState extends State<VpnApp> {
  final SessionViewModel viewModel = SessionViewModel();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'VPN 控制台',
      theme: ThemeData(
        useMaterial3: true,
        colorSchemeSeed: const Color(0xFF6C63FF),
        brightness: Brightness.light,
        scaffoldBackgroundColor: const Color(0xFFF7F8FC),
      ),
      darkTheme: ThemeData(
        useMaterial3: true,
        colorSchemeSeed: const Color(0xFF8D83FF),
        brightness: Brightness.dark,
      ),
      home: SessionsScreen(
        viewModel: viewModel,
        onImportProfile: () => showDialog<void>(
          context: context,
          builder: (_) => ProfileImportScreen(
            onImport: () => Navigator.of(context).pop(),
          ),
        ),
        onConnect: (session) => viewModel.updateStatus(session.id, VpnStatus.connecting),
        onDisconnect: (session) => viewModel.updateStatus(session.id, VpnStatus.disconnected),
        onEditStrategy: (session) => showDialog<RouteStrategy>(
          context: context,
          builder: (_) => RouteStrategyDialog(initial: session.routeStrategy),
        ).then((strategy) {
          if (strategy != null) {
            viewModel.updateStrategy(session.id, strategy);
          }
        }),
        onReorder: viewModel.reorderSessions,
      ),
    );
  }
}
