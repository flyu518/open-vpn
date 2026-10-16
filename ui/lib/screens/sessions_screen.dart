import 'package:flutter/material.dart';

import '../models/route_strategy.dart';
import '../models/session.dart';
import '../view_models/session_view_model.dart';

class SessionsScreen extends StatelessWidget {
  const SessionsScreen({
    super.key,
    required this.viewModel,
    this.onOpenProfiles,
    this.onConnect,
    this.onDisconnect,
    this.onEditName,
    this.onEditStrategy,
    this.onReorder,
  });

  final SessionViewModel viewModel;
  final VoidCallback? onOpenProfiles;
  final Future<void> Function(VpnSession)? onConnect;
  final Future<void> Function(VpnSession)? onDisconnect;
  final Future<void> Function(VpnSession)? onEditName;
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
          body: Stack(
            children: [
              const Positioned.fill(
                  child: IgnorePointer(child: _AmbientBackground())),
              SafeArea(
                child: Row(
                  children: [
                    _Sidebar(
                      onOpenProfiles: onOpenProfiles,
                    ),
                    Expanded(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(32, 28, 32, 24),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            _Header(
                              activeCount: activeCount,
                              profileCount: sessions.length,
                              warningCount: warningCount + errorCount,
                            ),
                            const SizedBox(height: 28),
                            _SectionHeader(
                              activeCount: activeCount,
                              sessionCount: sessions.length,
                            ),
                            const SizedBox(height: 12),
                            Expanded(
                              child: sessions.isEmpty
                                  ? _EmptyState(onOpenProfiles: onOpenProfiles)
                                  : ReorderableListView.builder(
                                      buildDefaultDragHandles: true,
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
                                              const EdgeInsets.only(bottom: 14),
                                          child: SessionCard(
                                            session: session,
                                            error:
                                                viewModel.errorFor(session.id),
                                            onConnect: () =>
                                                onConnect?.call(session),
                                            onDisconnect: () =>
                                                onDisconnect?.call(session),
                                            onEditName: () =>
                                                onEditName?.call(session),
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
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _AmbientBackground extends StatelessWidget {
  const _AmbientBackground();

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return DecoratedBox(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colors.surface,
            colors.surface,
            colors.primary.withValues(alpha: .055),
          ],
          stops: const [0, .55, 1],
        ),
      ),
      child: Stack(
        children: [
          Positioned(
            top: -170,
            right: -130,
            child: _GlowOrb(
              size: 420,
              color: colors.primary.withValues(alpha: .08),
            ),
          ),
          Positioned(
            bottom: -220,
            left: 210,
            child: _GlowOrb(
              size: 460,
              color: colors.tertiary.withValues(alpha: .045),
            ),
          ),
        ],
      ),
    );
  }
}

class _GlowOrb extends StatelessWidget {
  const _GlowOrb({required this.size, required this.color});

  final double size;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
      child: Container(
        width: size,
        height: size,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          gradient: RadialGradient(
            colors: [color, color.withValues(alpha: 0)],
          ),
        ),
      ),
    );
  }
}

class _Sidebar extends StatelessWidget {
  const _Sidebar({this.onOpenProfiles});

  final VoidCallback? onOpenProfiles;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      width: 250,
      margin: const EdgeInsets.all(12),
      padding: const EdgeInsets.fromLTRB(18, 20, 14, 16),
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withValues(alpha: .62),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: colors.outlineVariant.withValues(alpha: .5)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: .16),
            blurRadius: 30,
            offset: const Offset(0, 16),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const _BrandMark(),
              const SizedBox(width: 12),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('OpenVPN',
                      style: Theme.of(context).textTheme.titleMedium),
                  const SizedBox(height: 2),
                  Text('安全网络控制台',
                      style: Theme.of(context).textTheme.labelSmall),
                ],
              ),
            ],
          ),
          const SizedBox(height: 32),
          Text('工作区',
              style: Theme.of(context).textTheme.labelSmall?.copyWith(
                    color: colors.onSurfaceVariant,
                    letterSpacing: 1.2,
                  )),
          const SizedBox(height: 10),
          const _NavItem(
            icon: Icons.grid_view_rounded,
            label: '连接总览',
            detail: '实时状态与路由',
            selected: true,
          ),
          _NavItem(
            icon: Icons.folder_copy_outlined,
            label: '配置文件',
            detail: '管理 OpenVPN 配置',
            onTap: onOpenProfiles,
          ),
          _NavItem(
            icon: Icons.history_rounded,
            label: '连接记录',
            detail: '查看历史活动',
            onTap: () => ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('连接记录功能将在后续版本开放。')),
            ),
          ),
          const Spacer(),
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: colors.primary.withValues(alpha: .09),
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: colors.primary.withValues(alpha: .16)),
            ),
            child: Row(
              children: [
                Icon(Icons.shield_outlined, color: colors.primary, size: 22),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('安全模式',
                          style: Theme.of(context).textTheme.labelLarge),
                      const SizedBox(height: 3),
                      Text('兼容路由策略已启用',
                          style: Theme.of(context).textTheme.labelSmall),
                    ],
                  ),
                ),
                Icon(Icons.check_circle_rounded,
                    color: Colors.greenAccent.shade400, size: 17),
              ],
            ),
          ),
          const SizedBox(height: 12),
          const Text('OpenVPN · Windows / macOS',
              style: TextStyle(fontSize: 11)),
        ],
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
      width: 42,
      height: 42,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(14),
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [colors.primary, colors.tertiary],
        ),
        boxShadow: [
          BoxShadow(
            color: colors.primary.withValues(alpha: .34),
            blurRadius: 18,
            offset: const Offset(0, 7),
          ),
        ],
      ),
      child: Icon(Icons.shield_rounded, color: colors.onPrimary, size: 24),
    );
  }
}

class _NavItem extends StatelessWidget {
  const _NavItem({
    required this.icon,
    required this.label,
    required this.detail,
    this.selected = false,
    this.onTap,
  });

  final IconData icon;
  final String label;
  final String detail;
  final bool selected;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      margin: const EdgeInsets.only(top: 5),
      decoration: BoxDecoration(
        color: selected ? colors.primary.withValues(alpha: .13) : null,
        borderRadius: BorderRadius.circular(14),
        border: selected
            ? Border.all(color: colors.primary.withValues(alpha: .19))
            : null,
      ),
      child: Material(
        type: MaterialType.transparency,
        child: ListTile(
          dense: true,
          contentPadding:
              const EdgeInsets.symmetric(horizontal: 11, vertical: 3),
          leading: Icon(icon,
              size: 21,
              color: selected ? colors.primary : colors.onSurfaceVariant),
          title: Text(label),
          subtitle: Text(detail),
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
          onTap: onTap,
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
  });

  final int activeCount;
  final int profileCount;
  final int warningCount;

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
                  Container(
                    padding:
                        const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                    decoration: BoxDecoration(
                      color: colors.primary.withValues(alpha: .10),
                      borderRadius: BorderRadius.circular(30),
                    ),
                    child: Text(
                      '安全网络控制台',
                      style: Theme.of(context).textTheme.labelMedium?.copyWith(
                            color: colors.primary,
                            fontWeight: FontWeight.w700,
                            letterSpacing: .4,
                          ),
                    ),
                  ),
                  const SizedBox(height: 14),
                  Text('连接总览',
                      style: Theme.of(context).textTheme.displaySmall?.copyWith(
                            fontWeight: FontWeight.w700,
                            letterSpacing: -.7,
                          )),
                  const SizedBox(height: 6),
                  Text(
                    '管理多个独立 OpenVPN 网络，让每一条路由都清晰可控。',
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: colors.onSurfaceVariant,
                        ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: 16),
            const _ServicePill(),
          ],
        ),
        const SizedBox(height: 24),
        Wrap(
          spacing: 12,
          runSpacing: 12,
          children: [
            _MetricCard(
              icon: Icons.bolt_rounded,
              label: '活动连接',
              value: '$activeCount',
              accent: Colors.greenAccent,
            ),
            _MetricCard(
              icon: Icons.layers_rounded,
              label: '已载入配置',
              value: '$profileCount',
              accent: colors.primary,
            ),
            _MetricCard(
              icon: Icons.notifications_none_rounded,
              label: '策略提醒',
              value: '$warningCount',
              accent: warningCount == 0
                  ? colors.onSurfaceVariant
                  : Colors.orangeAccent,
            ),
          ],
        ),
      ],
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
      constraints: const BoxConstraints(minWidth: 145),
      padding: const EdgeInsets.fromLTRB(14, 12, 20, 12),
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withValues(alpha: .48),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: colors.outlineVariant.withValues(alpha: .6)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 32,
            height: 32,
            decoration: BoxDecoration(
              color: accent.withValues(alpha: .12),
              borderRadius: BorderRadius.circular(10),
            ),
            child: Icon(icon, color: accent, size: 18),
          ),
          const SizedBox(width: 10),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(label, style: Theme.of(context).textTheme.labelSmall),
              const SizedBox(height: 2),
              Text(value,
                  style: Theme.of(context).textTheme.titleLarge?.copyWith(
                        fontWeight: FontWeight.w700,
                      )),
            ],
          ),
        ],
      ),
    );
  }
}

class _ServicePill extends StatelessWidget {
  const _ServicePill();

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withValues(alpha: .55),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: colors.outlineVariant.withValues(alpha: .6)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.cloud_done_rounded,
              size: 17, color: Colors.greenAccent),
          const SizedBox(width: 7),
          Text('OpenVPN 服务', style: Theme.of(context).textTheme.labelMedium),
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
        Text('连接会话', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(width: 10),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          decoration: BoxDecoration(
            color: colors.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(30),
          ),
          child: Text('$sessionCount',
              style: Theme.of(context).textTheme.labelMedium),
        ),
        const Spacer(),
        Icon(Icons.drag_indicator_rounded,
            size: 17, color: colors.onSurfaceVariant),
        const SizedBox(width: 5),
        Text(
          activeCount > 0 ? '实时同步中' : '拖动调整优先级',
          style: Theme.of(context).textTheme.labelMedium?.copyWith(
                color: activeCount > 0
                    ? Colors.greenAccent
                    : colors.onSurfaceVariant,
              ),
        ),
      ],
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({this.onOpenProfiles});

  final VoidCallback? onOpenProfiles;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return SingleChildScrollView(
      padding: const EdgeInsets.only(bottom: 24),
      child: Center(
        child: Container(
          constraints: const BoxConstraints(maxWidth: 620),
          padding: const EdgeInsets.all(44),
          decoration: BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
              colors: [
                colors.surfaceContainerHighest.withValues(alpha: .66),
                colors.primary.withValues(alpha: .07),
              ],
            ),
            borderRadius: BorderRadius.circular(26),
            border:
                Border.all(color: colors.outlineVariant.withValues(alpha: .65)),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const _BrandMark(),
              const SizedBox(height: 20),
              Text('准备好连接你的网络',
                  style: Theme.of(context).textTheme.headlineSmall),
              const SizedBox(height: 9),
              Text('导入 OpenVPN 配置后，你可以同时管理多个公司网络。',
                  textAlign: TextAlign.center,
                  style: Theme.of(context).textTheme.bodyMedium),
              const SizedBox(height: 24),
              FilledButton.icon(
                onPressed: onOpenProfiles,
                icon: const Icon(Icons.folder_open_rounded),
                label: const Text('查看配置文件'),
              ),
            ],
          ),
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
    this.onEditName,
    this.onEditStrategy,
  });

  final VpnSession session;
  final String? error;
  final VoidCallback? onConnect;
  final VoidCallback? onDisconnect;
  final VoidCallback? onEditName;
  final VoidCallback? onEditStrategy;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final connected = session.status == VpnStatus.connected;
    final profileMissing = session.profile.trim().isEmpty;
    final accent = connected ? Colors.greenAccent : colors.primary;

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colors.surfaceContainerHighest.withValues(alpha: .72),
            accent.withValues(alpha: .055),
          ],
        ),
        borderRadius: BorderRadius.circular(22),
        border: Border.all(
          color: connected
              ? Colors.greenAccent.withValues(alpha: .30)
              : colors.outlineVariant.withValues(alpha: .72),
        ),
        boxShadow: [
          BoxShadow(
            color: accent.withValues(alpha: connected ? .08 : .025),
            blurRadius: 24,
            offset: const Offset(0, 12),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              _SessionIcon(connected: connected),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Flexible(
                          child: Text(session.name,
                              overflow: TextOverflow.ellipsis,
                              style: Theme.of(context).textTheme.titleMedium),
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
                    const SizedBox(height: 4),
                    Text(
                      session.remoteHost.isEmpty
                          ? '等待 OpenVPN 服务连接'
                          : session.remoteHost,
                      overflow: TextOverflow.ellipsis,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                            color: colors.onSurfaceVariant,
                          ),
                    ),
                  ],
                ),
              ),
              _StatusBadge(status: session.status),
            ],
          ),
          const SizedBox(height: 18),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              _SessionMetric(
                icon: Icons.public_rounded,
                label: '服务器',
                value: session.remoteHost.isEmpty ? '未连接' : session.remoteHost,
              ),
              _SessionMetric(
                icon: Icons.lan_rounded,
                label: '接口',
                value: session.interfaceName.isEmpty
                    ? '等待分配'
                    : session.interfaceName,
              ),
              _SessionMetric(
                icon: Icons.timer_outlined,
                label: '时长',
                value: _formatDuration(session.duration),
              ),
              _SessionMetric(
                icon: Icons.swap_vert_rounded,
                label: '流量',
                value:
                    _formatBytes(session.downloadBytes + session.uploadBytes),
              ),
            ],
          ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
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
                icon: const Icon(Icons.tune_rounded, size: 16),
                label: const Text('路由策略'),
                style: OutlinedButton.styleFrom(
                  visualDensity: VisualDensity.compact,
                  padding:
                      const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(11)),
                ),
              ),
              if (connected)
                OutlinedButton.icon(
                  onPressed: onDisconnect,
                  icon: const Icon(Icons.stop_circle_outlined, size: 16),
                  label: const Text('断开'),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: Colors.orangeAccent,
                    side: BorderSide(
                        color: Colors.orangeAccent.withValues(alpha: .42)),
                    visualDensity: VisualDensity.compact,
                    padding:
                        const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(11)),
                  ),
                )
              else
                FilledButton.icon(
                  onPressed: profileMissing ||
                          session.status == VpnStatus.connecting ||
                          onConnect == null
                      ? null
                      : onConnect,
                  icon: const Icon(Icons.bolt_rounded, size: 17),
                  label: Text(profileMissing ? '需要重新导入' : '连接'),
                  style: FilledButton.styleFrom(
                    visualDensity: VisualDensity.compact,
                    padding:
                        const EdgeInsets.symmetric(horizontal: 13, vertical: 9),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(11)),
                  ),
                ),
            ],
          ),
          if (session.routes.isNotEmpty) ...[
            const SizedBox(height: 14),
            _RouteLine(routes: session.routes),
          ],
          if (profileMissing) ...[
            const SizedBox(height: 10),
            Text(
              '当前会话没有本地 profile，请重新导入配置后再连接。',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: colors.onSurfaceVariant,
                  ),
            ),
          ],
          if (error != null) ...[
            const SizedBox(height: 14),
            _DiagnosticBox(
              icon: Icons.error_outline_rounded,
              message: error!,
              color: colors.error,
              hint: _isRouteConflict(error!)
                  ? '处理建议：请先断开提示中的前一个会话，再重试；也可以改用兼容模式。'
                  : null,
            ),
          ],
          if (session.warnings.isNotEmpty) ...[
            const SizedBox(height: 12),
            _DiagnosticBox(
              icon: Icons.warning_amber_rounded,
              message: session.warnings.join('\n'),
              color: Colors.orangeAccent,
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

class _SessionIcon extends StatelessWidget {
  const _SessionIcon({required this.connected});

  final bool connected;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final accent = connected ? Colors.greenAccent : colors.primary;
    return Container(
      width: 44,
      height: 44,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(14),
        color: accent.withValues(alpha: .12),
        border: Border.all(color: accent.withValues(alpha: .22)),
      ),
      child: Icon(
        connected ? Icons.shield_rounded : Icons.shield_outlined,
        color: accent,
        size: 23,
      ),
    );
  }
}

class _SessionMetric extends StatelessWidget {
  const _SessionMetric({
    required this.icon,
    required this.label,
    required this.value,
  });

  final IconData icon;
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      constraints: const BoxConstraints(minWidth: 128),
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
      decoration: BoxDecoration(
        color: colors.surface.withValues(alpha: .34),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: colors.outlineVariant.withValues(alpha: .45)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 16, color: colors.onSurfaceVariant),
          const SizedBox(width: 7),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(label, style: Theme.of(context).textTheme.labelSmall),
              const SizedBox(height: 2),
              ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 142),
                child: Text(value,
                    overflow: TextOverflow.ellipsis,
                    style: Theme.of(context).textTheme.bodySmall),
              ),
            ],
          ),
        ],
      ),
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
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
      decoration: BoxDecoration(
        color: colors.primary.withValues(alpha: .08),
        borderRadius: BorderRadius.circular(11),
        border: Border.all(color: colors.primary.withValues(alpha: .16)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 16, color: colors.primary),
          const SizedBox(width: 6),
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
        Icon(Icons.route_rounded, size: 17, color: colors.onSurfaceVariant),
        const SizedBox(width: 8),
        Expanded(
          child: Wrap(
            spacing: 6,
            runSpacing: 6,
            children: routes
                .map((route) => Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 9, vertical: 5),
                      decoration: BoxDecoration(
                        color: colors.surface.withValues(alpha: .45),
                        borderRadius: BorderRadius.circular(8),
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
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: color.withValues(alpha: .10),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withValues(alpha: .18)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 18, color: color),
          const SizedBox(width: 9),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(message,
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: color,
                        )),
                if (hint != null) ...[
                  const SizedBox(height: 6),
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
      VpnStatus.connected => (Colors.greenAccent, Icons.check_circle_rounded),
      VpnStatus.failed => (colors.error, Icons.error_rounded),
      VpnStatus.connecting => (colors.primary, Icons.sync_rounded),
      VpnStatus.disconnecting => (
          Colors.orangeAccent,
          Icons.sync_disabled_rounded
        ),
      VpnStatus.disconnected => (
          colors.onSurfaceVariant,
          Icons.pause_circle_outline_rounded
        ),
    };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      decoration: BoxDecoration(
        color: color.withValues(alpha: .11),
        borderRadius: BorderRadius.circular(30),
        border: Border.all(color: color.withValues(alpha: .18)),
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
