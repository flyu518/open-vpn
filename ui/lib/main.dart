import 'dart:io';

import 'package:flutter/material.dart';

import 'models/credentials.dart';
import 'models/session.dart';
import 'models/route_strategy.dart';
import 'screens/credentials_dialog.dart';
import 'screens/profile_import_screen.dart';
import 'screens/route_strategy_dialog.dart';
import 'screens/sessions_screen.dart';
import 'services/engine_client.dart';
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
  late final EngineClient engineClient;
  late final SessionViewModel viewModel;

  @override
  void initState() {
    super.initState();
    engineClient = Platform.isMacOS
        ? UnixSocketEngineClient()
        : UnavailableEngineClient('Windows named pipe 尚未接入，请先启动 Windows 服务端');
    viewModel = SessionViewModel(engineClient: engineClient);
    viewModel.loadSnapshot();
  }

  @override
  void dispose() {
    viewModel.dispose();
    if (engineClient case final UnixSocketEngineClient client) {
      client.dispose();
    } else if (engineClient case final InMemoryEngineClient client) {
      client.dispose();
    } else if (engineClient case final UnavailableEngineClient client) {
      client.dispose();
    }
    super.dispose();
  }

  Future<void> _importProfile() async {
    await showDialog<void>(
      context: context,
      builder: (_) => ProfileImportScreen(
        onImport: (profile) => viewModel.addImportedProfile(profile),
      ),
    );
  }

  Future<void> _connect(VpnSession session) async {
    final credentials = await showDialog<VpnCredentials>(
      context: context,
      builder: (_) => CredentialsDialog(profileName: session.name),
    );
    if (credentials == null) return;
    await viewModel.connectSession(
      session.id,
      username: credentials.username,
      password: credentials.password,
      rememberCredentials: credentials.remember,
    );
  }

  Future<void> _disconnect(VpnSession session) async {
    await viewModel.disconnectSession(session.id);
  }

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
        onImportProfile: _importProfile,
        onConnect: _connect,
        onDisconnect: _disconnect,
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
