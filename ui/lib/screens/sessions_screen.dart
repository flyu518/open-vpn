import 'package:flutter/material.dart';

import '../models/route_strategy.dart';
import '../models/session.dart';
import '../view_models/session_view_model.dart';

Color _successColor(BuildContext context) =>
    Theme.of(context).brightness == Brightness.dark
        ? const Color(0xFF77E0B0)
        : const Color(0xFF287A5C);

class SessionsScreen extends StatelessWidget {
  const SessionsScreen({
    super.key,
    required this.viewModel,
    this.onImportProfile,
    this.onRefreshService,
    this.onConnect,
    this.onDisconnect,
    this.onEditName,
    this.onViewDetails,
    this.onEditStrategy,
    this.onReorder,
  });

  final SessionViewModel viewModel;
  final VoidCallback? onImportProfile;
  final Future<void> Function()? onRefreshService;
  final Future<void> Function(VpnSession)? onConnect;
  final Future<void> Function(VpnSession)? onDisconnect;
  final Future<void> Function(VpnSession)? onEditName;
  final Future<void> Function(VpnSession)? onViewDetails;
  final ValueChanged<VpnSession>? onEditStrategy;
  final void Function(int oldIndex, int newIndex)? onReorder;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: viewModel,
      builder: (context, _) {
        final sessions = viewModel.sessions;
        final activeCount = sessions
            .where((session) => session.status == VpnStatus.connected)
            .length;
        final warningCount = sessions
            .map((session) => session.warnings.length)
            .fold<int>(0, (total, count) => total + count);
        final errorCount = sessions
            .map((session) => viewModel.errorFor(session.id) == null ? 0 : 1)
            .fold<int>(0, (total, count) => total + count);
        final colors = Theme.of(context).colorScheme;

        return Scaffold(
          backgroundColor: colors.surface,
          body: SafeArea(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const _Sidebar(),
                Expanded(
                  child: Align(
                    alignment: Alignment.topCenter,
                    child: ConstrainedBox(
                      constraints: const BoxConstraints(maxWidth: 1220),
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(32, 28, 32, 24),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            _Header(
                              activeCount: activeCount,
                              profileCount: sessions.length,
                              warningCount: warningCount + errorCount,
                              serviceError: viewModel.serviceError,
                              storageError: viewModel.profileStoreError,
                              onImportProfile: onImportProfile,
                              onRefreshService: onRefreshService,
                            ),
                            const SizedBox(height: 22),
                            _SectionHeader(
                              activeCount: activeCount,
                              sessionCount: sessions.length,
                            ),
                            const SizedBox(height: 10),
                            Expanded(
                              child: sessions.isEmpty
                                  ? _EmptyState(
                                      hasImportAction: onImportProfile != null,
                                    )
                                  : ReorderableListView.builder(
                                      buildDefaultDragHandles: false,
                                      itemCount: sessions.length,
                                      onReorderItem: (oldIndex, newIndex) =>
                                          onReorder?.call(oldIndex, newIndex),
                                      padding:
                                          const EdgeInsets.only(bottom: 24),
                                      itemBuilder: (context, index) {
                                        final session = sessions[index];
                                        return Padding(
                                          key: ValueKey(session.id),
                                          padding:
                                              const EdgeInsets.only(bottom: 10),
                                          child: SessionCard(
                                            session: session,
                                            index: index,
                                            serviceReady:
                                                viewModel.serviceChecked &&
                                                    viewModel.serviceError == null,
                                            error:
                                                viewModel.errorFor(session.id),
                                            onConnect: () =>
                                                onConnect?.call(session),
                                            onDisconnect: () =>
                                                onDisconnect?.call(session),
                                            onEditName: () =>
                                                onEditName?.call(session),
                                            onViewDetails: () =>
                                                onViewDetails?.call(session),
                                            onEditStrategy: () =>
                                                onEditStrategy?.call(session),
                                          ),
                                        );
                                      },
                                    ),
                            ),
                          ],
                        ),
                      ),
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
  const _Sidebar();

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      width: 224,
      margin: const EdgeInsets.fromLTRB(16, 16, 0, 16),
      padding: const EdgeInsets.fromLTRB(14, 16, 14, 14),
      decoration: BoxDecoration(
        color: colors.surfaceContainer,
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: colors.outlineVariant.withValues(alpha: .55)),
      ),
      child: const Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _BrandHeader(),
          SizedBox(height: 28),
          _SidebarLabel(text: '工作区'),
          SizedBox(height: 8),
          _NavItem(
            icon: Icons.grid_view_rounded,
            label: '连接总览',
            detail: '实时状态与路由',
            selected: true,
          ),
          Spacer(),
        ],
      ),
    );
  }
}

class _BrandHeader extends StatelessWidget {
  const _BrandHeader();

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        const _BrandMark(),
        const SizedBox(width: 10),
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('OpenVPN', style: Theme.of(context).textTheme.titleMedium),
            Text('多连接控制台',
                style: Theme.of(context).textTheme.labelSmall?.copyWith(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    )),
          ],
        ),
      ],
    );
  }
}

class _SidebarLabel extends StatelessWidget {
  const _SidebarLabel({required this.text});

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: Theme.of(context).textTheme.labelSmall?.copyWith(
            color: Theme.of(context).colorScheme.onSurfaceVariant,
            letterSpacing: .8,
            fontWeight: FontWeight.w700,
          ),
    );
  }
}

class _BrandMark extends StatelessWidget {
  const _BrandMark();

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      width: 38,
      height: 38,
      decoration: BoxDecoration(
        color: colors.primary,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Icon(Icons.shield_rounded, color: colors.onPrimary, size: 22),
    );
  }
}

class _NavItem extends StatelessWidget {
  const _NavItem({
    required this.icon,
    required this.label,
    required this.detail,
    this.selected = false,
  });

  final IconData icon;
  final String label;
  final String detail;
  final bool selected;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final foreground = selected ? colors.primary : colors.onSurfaceVariant;
    return Container(
      margin: const EdgeInsets.only(top: 4),
      decoration: BoxDecoration(
        color: selected ? colors.primary.withValues(alpha: .12) : null,
        borderRadius: BorderRadius.circular(12),
        border: selected
            ? Border.all(color: colors.primary.withValues(alpha: .22))
            : null,
      ),
      child: Material(
        type: MaterialType.transparency,
        child: ListTile(
        dense: true,
          minVerticalPadding: 6,
          contentPadding: const EdgeInsets.symmetric(horizontal: 10),
          leading: Icon(icon, size: 20, color: foreground),
          title: Text(label),
          subtitle: Text(detail),
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
          onTap: null,
        ),
      ),
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({
    required this.activeCount,
    required this.profileCount,
    required this.warningCount,
    required this.serviceError,
    this.storageError,
    this.onImportProfile,
    this.onRefreshService,
  });

  final int activeCount;
  final int profileCount;
  final int warningCount;
  final String? serviceError;
  final String? storageError;
  final VoidCallback? onImportProfile;
  final Future<void> Function()? onRefreshService;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('工作区 / 连接总览',
                      style: Theme.of(context).textTheme.labelMedium?.copyWith(
                            color: colors.primary,
                            fontWeight: FontWeight.w700,
                          )),
                  const SizedBox(height: 8),
                  Text('连接总览',
                      style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                            fontWeight: FontWeight.w700,
                            letterSpacing: -.5,
                          )),
                  const SizedBox(height: 5),
                  Text(
                    '同时管理多个 OpenVPN 连接，清楚看到每条连接的状态与路由。',
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colors.onSurfaceVariant,
                        ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: 20),
            Wrap(
              spacing: 10,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                IconButton.filled(
                  onPressed: onImportProfile,
                  tooltip: '导入 OpenVPN 配置',
                  icon: const Icon(Icons.add_rounded),
                ),
              ],
            ),
          ],
        ),
        if (serviceError != null) ...[
          const SizedBox(height: 16),
          _ServiceBanner(
            message: serviceError!,
            onRefresh: onRefreshService,
          ),
        ],
        if (storageError != null) ...[
          const SizedBox(height: 10),
          _ServiceBanner(message: storageError!),
        ],
        const SizedBox(height: 18),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            _MetricCard(
              icon: Icons.bolt_rounded,
              label: '活动连接',
              value: '$activeCount',
              accent: _successColor(context),
            ),
            _MetricCard(
              icon: Icons.layers_outlined,
              label: '已载入配置',
              value: '$profileCount',
              accent: colors.primary,
            ),
            _MetricCard(
              icon: Icons.warning_amber_rounded,
              label: '待处理',
              value: '$warningCount',
              accent: warningCount == 0 ? colors.onSurfaceVariant : colors.error,
            ),
          ],
        ),
      ],
    );
  }
}

class _ServiceBanner extends StatelessWidget {
  const _ServiceBanner({required this.message, this.onRefresh});

  final String message;
  final Future<void> Function()? onRefresh;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.fromLTRB(14, 12, 10, 12),
      decoration: BoxDecoration(
        color: colors.error.withValues(alpha: .08),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: colors.error.withValues(alpha: .22)),
      ),
      child: Row(
        children: [
          Icon(Icons.error_outline_rounded, size: 19, color: colors.error),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              message,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: colors.error,
                  ),
            ),
          ),
          if (onRefresh != null)
            TextButton.icon(
              onPressed: () => onRefresh!(),
              icon: const Icon(Icons.refresh_rounded, size: 16),
              label: const Text('重新检查'),
              style: TextButton.styleFrom(foregroundColor: colors.error),
            ),
        ],
      ),
    );
  }
}

class _MetricCard extends StatelessWidget {
  const _MetricCard({
    required this.icon,
    required this.label,
    required this.value,
    required this.accent,
  });

  final IconData icon;
  final String label;
  final String value;
  final Color accent;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      constraints: const BoxConstraints(minWidth: 148),
      padding: const EdgeInsets.fromLTRB(12, 10, 18, 10),
      decoration: BoxDecoration(
        color: colors.surfaceContainer,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: colors.outlineVariant.withValues(alpha: .55)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, color: accent, size: 18),
          const SizedBox(width: 9),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(label, style: Theme.of(context).textTheme.labelSmall),
              const SizedBox(height: 1),
              Text(value,
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w700,
                      )),
            ],
          ),
        ],
      ),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  const _SectionHeader({required this.activeCount, required this.sessionCount});

  final int activeCount;
  final int sessionCount;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Row(
      children: [
        Text('连接列表', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(width: 8),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
          decoration: BoxDecoration(
            color: colors.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(20),
          ),
          child: Text('$sessionCount',
              style: Theme.of(context).textTheme.labelMedium),
        ),
        const Spacer(),
        Icon(Icons.drag_handle_rounded,
            size: 17, color: colors.onSurfaceVariant),
        const SizedBox(width: 5),
        Text(
          activeCount > 0 ? '$activeCount 个连接活动中' : '拖动手柄调整优先级',
          style: Theme.of(context).textTheme.labelMedium?.copyWith(
                color: activeCount > 0
                    ? _successColor(context)
                    : colors.onSurfaceVariant,
              ),
        ),
      ],
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({
    required this.hasImportAction,
  });

  final bool hasImportAction;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Center(
      child: Container(
        width: double.infinity,
        constraints: const BoxConstraints(maxWidth: 620),
        padding: const EdgeInsets.all(34),
        decoration: BoxDecoration(
          color: colors.surfaceContainer,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: colors.outlineVariant.withValues(alpha: .55)),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.shield_outlined, size: 42, color: colors.primary),
            const SizedBox(height: 14),
            Text('还没有连接配置',
                style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 7),
            Text(
              '导入 .ovpn 或 .conf 文件后，可以在这里分别连接、断开并管理路由。',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: colors.onSurfaceVariant,
                  ),
            ),
            const SizedBox(height: 18),
            if (hasImportAction)
              Text(
                '请点击右上角“导入配置”开始。',
                style: Theme.of(context).textTheme.labelLarge?.copyWith(
                      color: colors.primary,
                    ),
              )
            else
              Text(
                '请从右上角加号导入配置。',
                style: Theme.of(context).textTheme.labelLarge?.copyWith(
                      color: colors.primary,
                    ),
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
    required this.index,
    required this.serviceReady,
    this.error,
    this.onConnect,
    this.onDisconnect,
    this.onEditName,
    this.onViewDetails,
    this.onEditStrategy,
  });

  final VpnSession session;
  final int index;
  final bool serviceReady;
  final String? error;
  final VoidCallback? onConnect;
  final VoidCallback? onDisconnect;
  final VoidCallback? onEditName;
  final VoidCallback? onViewDetails;
  final VoidCallback? onEditStrategy;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final connected = session.status == VpnStatus.connected;
    final profileMissing = session.profile.trim().isEmpty;

    return Container(
      padding: const EdgeInsets.fromLTRB(12, 12, 14, 12),
      decoration: BoxDecoration(
        color: colors.surfaceContainer,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(
          color: connected
              ? _successColor(context).withValues(alpha: .42)
              : colors.outlineVariant.withValues(alpha: .62),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              ReorderableDragStartListener(
                index: index,
                child: Tooltip(
                  message: '拖动调整优先级',
                  child: Padding(
                    padding: const EdgeInsets.only(right: 8),
                    child: Icon(Icons.drag_handle_rounded,
                        color: colors.onSurfaceVariant, size: 20),
                  ),
                ),
              ),
              _SessionIcon(connected: connected),
              const SizedBox(width: 11),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Flexible(
                          child: Text(
                            session.name,
                            overflow: TextOverflow.ellipsis,
                            style: Theme.of(context).textTheme.titleMedium?.copyWith(
                                  fontWeight: FontWeight.w700,
                                ),
                          ),
                        ),
                        IconButton(
                          tooltip: '编辑连接名称',
                          onPressed: onEditName,
                          visualDensity: VisualDensity.compact,
                          padding: EdgeInsets.zero,
                          constraints: const BoxConstraints.tightFor(
                              width: 28, height: 28),
                          icon: const Icon(Icons.edit_outlined, size: 16),
                        ),
                      ],
                    ),
                    const SizedBox(height: 2),
                    Text(
                      _subtitle(),
                      overflow: TextOverflow.ellipsis,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                            color: colors.onSurfaceVariant,
                          ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 12),
              _StatusBadge(status: session.status),
              const SizedBox(width: 10),
              _PrimaryAction(
                session: session,
                serviceReady: serviceReady,
                profileMissing: profileMissing,
                onConnect: onConnect,
                onDisconnect: onDisconnect,
              ),
            ],
          ),
          const SizedBox(height: 11),
          Wrap(
            spacing: 7,
            runSpacing: 7,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              _InfoChip(
                icon: Icons.public_rounded,
                label: session.remoteHost.isEmpty
                    ? '服务器待连接后显示'
                    : session.remoteHost,
              ),
              if (session.interfaceName.isNotEmpty)
                _InfoChip(
                  icon: Icons.lan_rounded,
                  label: session.interfaceName,
                ),
              _StrategyChip(
                icon: Icons.alt_route_rounded,
                label: _strategyLabel(session),
              ),
              _StrategyChip(
                icon: Icons.low_priority_rounded,
                label: '优先级 ${session.routeStrategy.priority}',
              ),
              OutlinedButton.icon(
                onPressed: onEditStrategy,
                icon: const Icon(Icons.tune_rounded, size: 15),
                label: const Text('路由策略'),
                style: OutlinedButton.styleFrom(
                  visualDensity: VisualDensity.compact,
                  padding:
                      const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(9)),
                  ),
                ),
              OutlinedButton.icon(
                onPressed: onViewDetails,
                icon: const Icon(Icons.info_outline_rounded, size: 15),
                label: const Text('查看详情'),
                style: OutlinedButton.styleFrom(
                  visualDensity: VisualDensity.compact,
                  padding:
                      const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(9)),
                ),
              ),
              if (connected)
                _ConnectionStat(
                  icon: Icons.timer_outlined,
                  value: _formatDuration(session.duration),
                ),
              if (connected)
                _ConnectionStat(
                  icon: Icons.swap_vert_rounded,
                  value: _formatBytes(
                      session.downloadBytes + session.uploadBytes),
                ),
            ],
          ),
          if (session.routes.isNotEmpty) ...[
            const SizedBox(height: 10),
            _RouteLine(routes: session.routes),
          ],
          if (profileMissing) ...[
            const SizedBox(height: 10),
            _DiagnosticBox(
              icon: Icons.description_outlined,
              message: '配置文件不在原路径，重新导入后才能连接。',
              color: colors.error,
            ),
          ],
          if (error != null) ...[
            const SizedBox(height: 10),
            _DiagnosticBox(
              icon: Icons.error_outline_rounded,
              message: error!,
              color: colors.error,
              hint: _isRouteConflict(error!)
                  ? '请先断开提示中的前一个会话，再重试；也可以将策略改为兼容模式。'
                  : null,
            ),
          ],
          if (session.warnings.isNotEmpty) ...[
            const SizedBox(height: 10),
            _DiagnosticBox(
              icon: Icons.warning_amber_rounded,
              message: session.warnings.join('\n'),
              color: Colors.orange.shade300,
            ),
          ],
        ],
      ),
    );
  }

  String _subtitle() {
    final parts = <String>[];
    if (session.username.isNotEmpty) parts.add('用户 ${session.username}');
    if (session.remoteHost.isNotEmpty) parts.add(session.remoteHost);
    if (parts.isEmpty) return '尚未连接';
    return parts.join('  ·  ');
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

  String _strategyLabel(VpnSession value) {
    switch (value.routeStrategy.conflictMode) {
      case RouteConflictMode.compatible:
        return '兼容模式';
      case RouteConflictMode.strict:
        return '严格模式';
      case RouteConflictMode.ask:
        return '询问模式';
    }
  }

  bool _isRouteConflict(String value) =>
      value.contains('冲突') || value.contains('重叠');
}

class _PrimaryAction extends StatelessWidget {
  const _PrimaryAction({
    required this.session,
    required this.serviceReady,
    required this.profileMissing,
    this.onConnect,
    this.onDisconnect,
  });

  final VpnSession session;
  final bool serviceReady;
  final bool profileMissing;
  final VoidCallback? onConnect;
  final VoidCallback? onDisconnect;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    if (session.status == VpnStatus.connected) {
      return OutlinedButton.icon(
        onPressed: onDisconnect,
        icon: const Icon(Icons.stop_rounded, size: 16),
        label: const Text('断开'),
        style: OutlinedButton.styleFrom(
          foregroundColor: colors.error,
          side: BorderSide(color: colors.error.withValues(alpha: .42)),
          visualDensity: VisualDensity.compact,
          padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
        ),
      );
    }

    if (session.status == VpnStatus.disconnecting) {
      return OutlinedButton.icon(
        onPressed: null,
        icon: const Icon(Icons.sync_disabled_rounded, size: 16),
        label: const Text('断开中…'),
        style: OutlinedButton.styleFrom(
          visualDensity: VisualDensity.compact,
          padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
        ),
      );
    }

    final connecting = session.status == VpnStatus.connecting;
    final unavailable = !serviceReady;
    final disabled = profileMissing || connecting || unavailable;
    final label = profileMissing
        ? '需重新导入'
        : connecting
            ? '连接中…'
            : unavailable
                ? '服务未就绪'
                : session.status == VpnStatus.failed
                    ? '重试连接'
                    : '连接';
    return FilledButton.icon(
      onPressed: disabled ? null : onConnect,
      icon: Icon(
          unavailable ? Icons.cloud_off_rounded : Icons.play_arrow_rounded,
          size: 17),
      label: Text(label),
      style: FilledButton.styleFrom(
        visualDensity: VisualDensity.compact,
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
      ),
    );
  }
}

class _SessionIcon extends StatelessWidget {
  const _SessionIcon({required this.connected});

  final bool connected;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final accent = connected ? _successColor(context) : colors.primary;
    return Container(
      width: 38,
      height: 38,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(11),
        color: accent.withValues(alpha: .12),
      ),
      child: Icon(
        connected ? Icons.shield_rounded : Icons.shield_outlined,
        color: accent,
        size: 21,
      ),
    );
  }
}

class _InfoChip extends StatelessWidget {
  const _InfoChip({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 7),
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withValues(alpha: .65),
        borderRadius: BorderRadius.circular(9),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 15, color: colors.onSurfaceVariant),
          const SizedBox(width: 6),
          ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 240),
            child: Text(label,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.labelMedium),
          ),
        ],
      ),
    );
  }
}

class _ConnectionStat extends StatelessWidget {
  const _ConnectionStat({required this.icon, required this.value});

  final IconData icon;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon,
            size: 15, color: Theme.of(context).colorScheme.onSurfaceVariant),
        const SizedBox(width: 5),
        Text(value, style: Theme.of(context).textTheme.labelMedium),
      ],
    );
  }
}

class _StrategyChip extends StatelessWidget {
  const _StrategyChip({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 7),
      decoration: BoxDecoration(
        color: colors.primary.withValues(alpha: .08),
        borderRadius: BorderRadius.circular(9),
        border: Border.all(color: colors.primary.withValues(alpha: .15)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 15, color: colors.primary),
          const SizedBox(width: 5),
          Text(label, style: Theme.of(context).textTheme.labelMedium),
        ],
      ),
    );
  }
}

class _RouteLine extends StatelessWidget {
  const _RouteLine({required this.routes});

  final List<String> routes;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(Icons.route_rounded, size: 16, color: colors.onSurfaceVariant),
        const SizedBox(width: 8),
        Expanded(
          child: Wrap(
            spacing: 6,
            runSpacing: 6,
            children: routes
                .map((route) => Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 8, vertical: 4),
                      decoration: BoxDecoration(
                        color: colors.surfaceContainerHighest,
                        borderRadius: BorderRadius.circular(7),
                      ),
                      child: Text(route,
                          style: Theme.of(context).textTheme.labelSmall),
                    ))
                .toList(),
          ),
        ),
      ],
    );
  }
}

class _DiagnosticBox extends StatelessWidget {
  const _DiagnosticBox({
    required this.icon,
    required this.message,
    required this.color,
    this.hint,
  });

  final IconData icon;
  final String message;
  final Color color;
  final String? hint;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
      decoration: BoxDecoration(
        color: color.withValues(alpha: .08),
        borderRadius: BorderRadius.circular(9),
        border: Border.all(color: color.withValues(alpha: .18)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 17, color: color),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(message,
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: color,
                        )),
                if (hint != null) ...[
                  const SizedBox(height: 4),
                  Text(hint!,
                      style: Theme.of(context).textTheme.labelSmall?.copyWith(
                            color: color,
                          )),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _StatusBadge extends StatelessWidget {
  const _StatusBadge({required this.status});

  final VpnStatus status;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final (color, icon) = switch (status) {
      VpnStatus.connected => (
          _successColor(context),
          Icons.check_circle_rounded
        ),
      VpnStatus.failed => (colors.error, Icons.error_rounded),
      VpnStatus.connecting => (colors.primary, Icons.sync_rounded),
      VpnStatus.disconnecting => (
          Colors.orange.shade300,
          Icons.sync_disabled_rounded
        ),
      VpnStatus.disconnected => (
          colors.onSurfaceVariant,
          Icons.pause_circle_outline_rounded
        ),
    };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 7),
      decoration: BoxDecoration(
        color: color.withValues(alpha: .10),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: color.withValues(alpha: .20)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 14, color: color),
          const SizedBox(width: 5),
          Text(status.label,
              style: Theme.of(context).textTheme.labelSmall?.copyWith(
                    color: color,
                    fontWeight: FontWeight.w700,
                  )),
        ],
      ),
    );
  }
}
