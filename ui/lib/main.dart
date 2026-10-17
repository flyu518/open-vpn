import 'dart:convert';
import 'dart:io';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import 'models/credentials.dart';
import 'models/session.dart';
import 'models/route_strategy.dart';
import 'screens/credentials_dialog.dart';
import 'screens/profile_import_screen.dart';
import 'screens/profile_details_dialog.dart';
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
        : Platform.isWindows
            ? WindowsNamedPipeEngineClient()
            : UnavailableEngineClient('当前平台暂未接入 OpenVPN 服务端');
    profileStore = JsonProfileStore();
    viewModel = SessionViewModel(
      engineClient: engineClient,
      profileStore: profileStore,
    );
    _loadInitialState();
  }

  Future<void> _loadInitialState() async {
    await viewModel.loadProfiles();
    // 安装器/launchd 刚启动时 socket 可能还在创建，启动阶段自动重试，
    // 避免用户打开应用就看到“服务未启动”而必须手动点击检查。
    for (var attempt = 0; attempt < 10; attempt++) {
      await viewModel.loadSnapshot();
      if (viewModel.serviceError == null) return;
      if (attempt < 9) {
        await Future<void>.delayed(const Duration(milliseconds: 500));
      }
    }
  }

  @override
  void dispose() {
    viewModel.dispose();
    if (engineClient case final UnixSocketEngineClient client) {
      client.dispose();
    } else if (engineClient case final WindowsNamedPipeEngineClient client) {
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
    try {
      final profile = await pickOpenVpnProfile();
      if (profile == null) return;
      await viewModel.addImportedProfile(profile);
      if (navigatorContext.mounted) {
        ScaffoldMessenger.of(navigatorContext).showSnackBar(
          SnackBar(content: Text('已导入：${profile.displayName}')),
        );
      }
    } on Object catch (error) {
      if (navigatorContext.mounted) {
        ScaffoldMessenger.of(navigatorContext).showSnackBar(
          SnackBar(content: Text('导入配置失败：$error')),
        );
      }
    }
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

  Future<void> _viewDetails(VpnSession session) async {
    final context = _navigatorKey.currentState?.context;
    if (context == null) return;
    await showDialog<void>(
      context: context,
      builder: (_) => ProfileDetailsDialog(
        session: session,
        onExport: () => _exportProfile(session),
        onReimport: () => _reimportProfile(session),
      ),
    );
  }

  Future<void> _reimportProfile(VpnSession session) async {
    final profile = await pickOpenVpnProfile();
    if (profile == null) return;
    await viewModel.replaceImportedProfile(session.id, profile);
  }

  Future<void> _exportProfile(VpnSession session) async {
    final context = _navigatorKey.currentState?.context;
    if (context == null) return;
    if (session.profile.trim().isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('当前配置不可读，请先重新导入')),
      );
      return;
    }
    try {
      const typeGroup = XTypeGroup(
        label: 'OpenVPN 配置',
        extensions: <String>['ovpn', 'conf'],
      );
      final location = await getSaveLocation(
        acceptedTypeGroups: const <XTypeGroup>[typeGroup],
        suggestedName: '${_safeFileName(session.name)}.ovpn',
      );
      if (location == null) return;
      await XFile.fromData(
        utf8.encode(session.profile),
        mimeType: 'application/x-openvpn-profile',
        name: _safeFileName(session.name),
      ).saveTo(location.path);
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('配置已导出到：${location.path}')),
        );
      }
    } on Object catch (error) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('导出配置失败：$error')),
        );
      }
    }
  }

  String _safeFileName(String value) {
    final normalized = value
        .replaceAll(RegExp(r'[\\/:*?"<>|]'), '_')
        .trim();
    return normalized.isEmpty ? 'openvpn-profile' : normalized;
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      navigatorKey: _navigatorKey,
      title: 'OpenVPN',
      theme: ThemeData(
        useMaterial3: true,
        colorSchemeSeed: const Color(0xFF5276D9),
        brightness: Brightness.light,
        scaffoldBackgroundColor: const Color(0xFFF5F7FB),
      ),
      darkTheme: ThemeData(
        useMaterial3: true,
        colorSchemeSeed: const Color(0xFF7898F2),
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF11151D),
      ),
      themeMode: ThemeMode.system,
      home: SessionsScreen(
        viewModel: viewModel,
        onImportProfile: _importProfile,
        onRefreshService: viewModel.loadSnapshot,
        onConnect: _connect,
        onDisconnect: _disconnect,
        onEditName: _editName,
        onViewDetails: _viewDetails,
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
