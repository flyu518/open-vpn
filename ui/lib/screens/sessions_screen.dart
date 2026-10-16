import 'package:flutter/material.dart';

import '../models/session.dart';
import '../view_models/session_view_model.dart';

class SessionsScreen extends StatelessWidget {
  const SessionsScreen({
    super.key,
    required this.viewModel,
    this.onImportProfile,
    this.onConnect,
    this.onDisconnect,
    this.onEditStrategy,
  });

  final SessionViewModel viewModel;
  final VoidCallback? onImportProfile;
  final ValueChanged<VpnSession>? onConnect;
  final ValueChanged<VpnSession>? onDisconnect;
  final ValueChanged<VpnSession>? onEditStrategy;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: viewModel,
      builder: (context, _) {
        final theme = Theme.of(context);
        return Scaffold(
          backgroundColor: theme.colorScheme.surface,
          body: SafeArea(
            child: Row(
              children: [
                _Sidebar(onImportProfile: onImportProfile),
                Expanded(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(32, 28, 32, 24),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        _Header(
                          activeLabel: viewModel.activeLabel,
                          onImportProfile: onImportProfile,
                        ),
                        const SizedBox(height: 28),
                        Expanded(
                          child: viewModel.sessions.isEmpty
                              ? _EmptyState(onImportProfile: onImportProfile)
                              : ListView.separated(
                                  itemCount: viewModel.sessions.length,
                                  separatorBuilder: (_, __) =>
                                      const SizedBox(height: 14),
                                  itemBuilder: (context, index) {
                                    final session = viewModel.sessions[index];
                                    return SessionCard(
                                      session: session,
                                      error: viewModel.errorFor(session.id),
                                      onConnect: () => onConnect?.call(session),
                                      onDisconnect: () =>
                                          onDisconnect?.call(session),
                                      onEditStrategy: () =>
                                          onEditStrategy?.call(session),
                                    );
                                  },
                                ),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _Sidebar extends StatelessWidget {
  const _Sidebar({this.onImportProfile});

  final VoidCallback? onImportProfile;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      width: 230,
      padding: const EdgeInsets.fromLTRB(20, 24, 16, 20),
      color: colors.surfaceContainerHighest.withOpacity(.45),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 34,
                height: 34,
                decoration: BoxDecoration(
                  color: colors.primary,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Icon(Icons.shield_rounded, color: colors.onPrimary),
              ),
              const SizedBox(width: 10),
              Text('VPN 控制台', style: Theme.of(context).textTheme.titleMedium),
            ],
          ),
          const SizedBox(height: 34),
          _NavItem(icon: Icons.dashboard_rounded, label: '连接总览', selected: true),
          _NavItem(icon: Icons.folder_rounded, label: '配置文件'),
          _NavItem(icon: Icons.history_rounded, label: '连接记录'),
          const Spacer(),
          OutlinedButton.icon(
            onPressed: onImportProfile,
            icon: const Icon(Icons.add_rounded, size: 18),
            label: const Text('导入配置'),
            style: OutlinedButton.styleFrom(
              minimumSize: const Size.fromHeight(42),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(12),
              ),
            ),
          ),
          const SizedBox(height: 10),
          const Text('Windows / macOS 桌面版', style: TextStyle(fontSize: 11)),
        ],
      ),
    );
  }
}

class _NavItem extends StatelessWidget {
  const _NavItem({required this.icon, required this.label, this.selected = false});

  final IconData icon;
  final String label;
  final bool selected;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      margin: const EdgeInsets.only(bottom: 6),
      decoration: BoxDecoration(
        color: selected ? colors.primary.withOpacity(.12) : Colors.transparent,
        borderRadius: BorderRadius.circular(10),
      ),
      child: ListTile(
        dense: true,
        leading: Icon(icon, size: 20, color: selected ? colors.primary : null),
        title: Text(label),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        onTap: () {},
      ),
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({required this.activeLabel, this.onImportProfile});

  final String activeLabel;
  final VoidCallback? onImportProfile;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('连接总览', style: Theme.of(context).textTheme.headlineSmall),
            const SizedBox(height: 6),
            Text(activeLabel, style: Theme.of(context).textTheme.bodyMedium),
          ],
        ),
        const Spacer(),
        FilledButton.icon(
          onPressed: onImportProfile,
          icon: const Icon(Icons.add_rounded),
          label: const Text('添加 VPN'),
          style: FilledButton.styleFrom(
            padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(12),
            ),
          ),
        ),
      ],
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({this.onImportProfile});

  final VoidCallback? onImportProfile;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Center(
      child: Container(
        constraints: const BoxConstraints(maxWidth: 520),
        padding: const EdgeInsets.all(44),
        decoration: BoxDecoration(
          color: colors.surfaceContainerHighest.withOpacity(.45),
          borderRadius: BorderRadius.circular(24),
          border: Border.all(color: colors.outlineVariant),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.shield_moon_rounded, size: 56, color: colors.primary),
            const SizedBox(height: 18),
            Text('还没有 VPN 连接', style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 8),
            const Text('导入 .ovpn 配置后，可以同时管理多个公司网络。'),
            const SizedBox(height: 22),
            FilledButton.icon(
              onPressed: onImportProfile,
              icon: const Icon(Icons.upload_file_rounded),
              label: const Text('导入配置文件'),
            ),
          ],
        ),
      ),
    );
  }
}

class SessionCard extends StatelessWidget {
  const SessionCard({
    super.key,
    required this.session,
    this.error,
    this.onConnect,
    this.onDisconnect,
    this.onEditStrategy,
  });

  final VpnSession session;
  final String? error;
  final VoidCallback? onConnect;
  final VoidCallback? onDisconnect;
  final VoidCallback? onEditStrategy;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final connected = session.status == VpnStatus.connected;
    final accent = connected ? Colors.green : colors.primary;
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withOpacity(.42),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(
          color: connected ? Colors.green.withOpacity(.35) : colors.outlineVariant,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 10,
                height: 10,
                decoration: BoxDecoration(color: accent, shape: BoxShape.circle),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Text(session.name,
                    style: Theme.of(context).textTheme.titleMedium),
              ),
              _StatusBadge(status: session.status),
              const SizedBox(width: 12),
              IconButton(
                tooltip: '路由策略',
                onPressed: onEditStrategy,
                icon: const Icon(Icons.tune_rounded),
              ),
              if (connected)
                OutlinedButton(
                  onPressed: onDisconnect,
                  child: const Text('断开'),
                )
              else
                FilledButton(
                  onPressed: onConnect,
                  child: const Text('连接'),
                ),
            ],
          ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 22,
            runSpacing: 8,
            children: [
              _InfoItem(icon: Icons.public_rounded, label: '服务器', value: session.remoteHost.isEmpty ? '未连接' : session.remoteHost),
              _InfoItem(icon: Icons.lan_rounded, label: '接口', value: session.interfaceName.isEmpty ? '等待分配' : session.interfaceName),
              _InfoItem(icon: Icons.timer_outlined, label: '时长', value: _formatDuration(session.duration)),
              _InfoItem(icon: Icons.swap_vert_rounded, label: '流量', value: _formatBytes(session.downloadBytes + session.uploadBytes)),
            ],
          ),
          if (session.routes.isNotEmpty) ...[
            const SizedBox(height: 14),
            Wrap(
              spacing: 6,
              runSpacing: 6,
              children: session.routes
                  .map((route) => Chip(
                        label: Text(route),
                        visualDensity: VisualDensity.compact,
                        side: BorderSide.none,
                      ))
                  .toList(),
            ),
          ],
          if (error != null) ...[
            const SizedBox(height: 12),
            Text(error!, style: TextStyle(color: colors.error)),
          ],
          if (session.warnings.isNotEmpty) ...[
            const SizedBox(height: 12),
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: colors.tertiaryContainer,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(Icons.warning_amber_rounded,
                      size: 18, color: colors.onTertiaryContainer),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      session.warnings.join('\n'),
                      style: TextStyle(color: colors.onTertiaryContainer),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }

  String _formatDuration(Duration value) {
    final minutes = value.inMinutes.remainder(60).toString().padLeft(2, '0');
    final seconds = value.inSeconds.remainder(60).toString().padLeft(2, '0');
    return '${value.inHours.toString().padLeft(2, '0')}:$minutes:$seconds';
  }

  String _formatBytes(int bytes) {
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(0)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }
}

class _StatusBadge extends StatelessWidget {
  const _StatusBadge({required this.status});

  final VpnStatus status;

  @override
  Widget build(BuildContext context) {
    final color = status == VpnStatus.connected
        ? Colors.green
        : status == VpnStatus.failed
            ? Theme.of(context).colorScheme.error
            : Theme.of(context).colorScheme.primary;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: color.withOpacity(.12),
        borderRadius: BorderRadius.circular(20),
      ),
      child: Text(status.label, style: TextStyle(color: color, fontSize: 12)),
    );
  }
}

class _InfoItem extends StatelessWidget {
  const _InfoItem({required this.icon, required this.label, required this.value});

  final IconData icon;
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 17),
        const SizedBox(width: 7),
        Text('$label  ', style: Theme.of(context).textTheme.bodySmall),
        Text(value, style: Theme.of(context).textTheme.bodyMedium),
      ],
    );
  }
}
