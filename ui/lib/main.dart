import 'dart:io';

import 'package:flutter/material.dart';

import 'models/credentials.dart';
import 'models/session.dart';
import 'models/route_strategy.dart';
import 'screens/credentials_dialog.dart';
import 'screens/profile_library_screen.dart';
import 'screens/profile_import_screen.dart';
import 'screens/route_strategy_dialog.dart';
import 'screens/session_name_dialog.dart';
import 'screens/sessions_screen.dart';
import 'services/engine_client.dart';
import 'services/profile_store.dart';
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
  final GlobalKey<NavigatorState> _navigatorKey = GlobalKey<NavigatorState>();
  late final EngineClient engineClient;
  late final SessionViewModel viewModel;
  late final ProfileStore profileStore;

  @override
  void initState() {
    super.initState();
    engineClient = Platform.isMacOS
        ? UnixSocketEngineClient()
        : UnavailableEngineClient('Windows named pipe 尚未接入，请先启动 Windows 服务端');
    profileStore = JsonProfileStore();
    viewModel = SessionViewModel(
      engineClient: engineClient,
      profileStore: profileStore,
    );
    _loadInitialState();
  }

  Future<void> _loadInitialState() async {
    await viewModel.loadProfiles();
    await viewModel.loadSnapshot();
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
    final navigatorContext = _navigatorKey.currentState?.context;
    if (navigatorContext == null) return;
    await showDialog<void>(
      context: navigatorContext,
      builder: (_) => ProfileImportScreen(
        onImport: (profile) => viewModel.addImportedProfile(profile),
      ),
    );
  }

  Future<void> _openProfiles() async {
    final navigatorContext = _navigatorKey.currentState?.context;
    if (navigatorContext == null) return;
    await showDialog<void>(
      context: navigatorContext,
      builder: (_) => ProfileLibraryDialog(
        sessions: viewModel.sessions,
        onImport: _importProfile,
      ),
    );
  }

  Future<void> _connect(VpnSession session) async {
    final navigatorContext = _navigatorKey.currentState?.context;
    if (navigatorContext == null) return;
    final credentials = await showDialog<VpnCredentials>(
      context: navigatorContext,
      builder: (_) => CredentialsDialog(
        profileName: session.name,
        initialUsername: session.username,
      ),
    );
    if (credentials == null) return;
    await viewModel.connectSession(
      session.id,
      username: credentials.username,
      password: credentials.password,
      rememberCredentials: credentials.remember,
    );
  }

  Future<void> _editName(VpnSession session) async {
    final navigatorContext = _navigatorKey.currentState?.context;
    if (navigatorContext == null) return;
    final name = await showDialog<String>(
      context: navigatorContext,
      builder: (_) => SessionNameDialog(initialName: session.name),
    );
    if (name != null) viewModel.renameSession(session.id, name);
  }

  Future<void> _disconnect(VpnSession session) async {
    await viewModel.disconnectSession(session.id);
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      navigatorKey: _navigatorKey,
      title: 'OpenVPN',
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
        scaffoldBackgroundColor: const Color(0xFF090D18),
      ),
      themeMode: ThemeMode.dark,
      home: SessionsScreen(
        viewModel: viewModel,
        onOpenProfiles: _openProfiles,
        onConnect: _connect,
        onDisconnect: _disconnect,
        onEditName: _editName,
        onEditStrategy: (session) => showDialog<RouteStrategy>(
          context: _navigatorKey.currentState!.context,
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
