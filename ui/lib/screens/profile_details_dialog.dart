import 'package:flutter/material.dart';

import '../models/session.dart';

class ProfileDetailsDialog extends StatelessWidget {
  const ProfileDetailsDialog({
    super.key,
    required this.session,
    this.onExport,
    this.onReimport,
  });

  final VpnSession session;
  final Future<void> Function()? onExport;
  final Future<void> Function()? onReimport;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final lines = readableProfileLines(session.profile);
    return AlertDialog(
      title: Row(
        children: [
          Expanded(
            child: Text(session.name, overflow: TextOverflow.ellipsis),
          ),
          _StatusDot(status: session.status),
        ],
      ),
      content: SizedBox(
        width: 680,
        height: 480,
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _InfoGrid(session: session),
              const SizedBox(height: 22),
              Text('连接记录', style: Theme.of(context).textTheme.titleSmall),
              const SizedBox(height: 8),
              _ActivityGrid(session: session),
              const SizedBox(height: 22),
              Text('配置摘要', style: Theme.of(context).textTheme.titleSmall),
              const SizedBox(height: 8),
              if (lines.isEmpty)
                Text(
                  '配置文件暂时不可读，请重新导入。',
                  style: TextStyle(color: colors.onSurfaceVariant),
                )
              else
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: colors.surfaceContainerHighest.withValues(alpha: .55),
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: colors.outlineVariant),
                  ),
                  child: SelectableText(
                    lines.join('\n'),
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          fontFamily: 'Menlo',
                          height: 1.55,
                        ),
                  ),
                ),
              const SizedBox(height: 12),
              Text(
                '密码、证书、私钥和挑战响应不会在这里展示。',
                style: Theme.of(context).textTheme.labelSmall?.copyWith(
                      color: colors.onSurfaceVariant,
                    ),
              ),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: onReimport == null
              ? null
              : () async {
                  await onReimport!();
                  if (context.mounted) Navigator.of(context).pop();
                },
          child: const Text('重新导入'),
        ),
        TextButton.icon(
          onPressed: onExport == null ? null : () => onExport!(),
          icon: const Icon(Icons.file_download_outlined, size: 17),
          label: const Text('导出配置'),
        ),
        FilledButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('关闭'),
        ),
      ],
    );
  }
}

class _InfoGrid extends StatelessWidget {
  const _InfoGrid({required this.session});

  final VpnSession session;

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 10,
      runSpacing: 10,
      children: [
        _InfoTile(label: '状态', value: session.status.label),
        _InfoTile(
          label: '用户名',
          value: session.username.isEmpty ? '未读取' : session.username,
        ),
        _InfoTile(
          label: '服务器',
          value: session.remoteHost.isEmpty ? '连接后显示' : session.remoteHost,
        ),
        _InfoTile(
          label: '配置路径',
          value: session.profilePath.isEmpty ? '未记录' : session.profilePath,
          wide: true,
        ),
      ],
    );
  }
}

class _ActivityGrid extends StatelessWidget {
  const _ActivityGrid({required this.session});

  final VpnSession session;

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 10,
      runSpacing: 10,
      children: [
        _InfoTile(
          label: '连接次数',
          value: session.connectionCount == 0
              ? '暂无记录'
              : '${session.connectionCount} 次',
        ),
        _InfoTile(
          label: '最近连接',
          value: formatSessionTime(session.lastConnectedAt),
        ),
        _InfoTile(
          label: '最近断开',
          value: formatSessionTime(session.lastDisconnectedAt),
        ),
      ],
    );
  }
}

class _InfoTile extends StatelessWidget {
  const _InfoTile({
    required this.label,
    required this.value,
    this.wide = false,
  });

  final String label;
  final String value;
  final bool wide;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return Container(
      constraints: BoxConstraints(minWidth: wide ? 620 : 190, maxWidth: 650),
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
      decoration: BoxDecoration(
        color: colors.surfaceContainerHighest.withValues(alpha: .45),
        borderRadius: BorderRadius.circular(10),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: Theme.of(context).textTheme.labelSmall),
          const SizedBox(height: 3),
          Text(value,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: Theme.of(context).textTheme.bodySmall),
        ],
      ),
    );
  }
}

class _StatusDot extends StatelessWidget {
  const _StatusDot({required this.status});

  final VpnStatus status;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final color = switch (status) {
      VpnStatus.connected => colors.tertiary,
      VpnStatus.failed => colors.error,
      VpnStatus.connecting || VpnStatus.disconnecting => colors.primary,
      VpnStatus.disconnected => colors.onSurfaceVariant,
    };
    return Container(
      width: 10,
      height: 10,
      decoration: BoxDecoration(color: color, shape: BoxShape.circle),
    );
  }
}

List<String> readableProfileLines(String contents) {
  const sensitiveTags = <String>{
    'ca',
    'cert',
    'key',
    'private-key',
    'tls-auth',
    'tls-crypt',
    'pkcs12',
    'secret',
  };
  const visibleCommands = <String>{
    'client',
    'dev',
    'dev-type',
    'proto',
    'remote',
    'remote-random',
    'nobind',
    'route',
    'route-ipv6',
    'route-nopull',
    'redirect-gateway',
    'topology',
    'auth-nocache',
    'cipher',
    'data-ciphers',
    'verify-x509-name',
    'dhcp-option',
  };
  final result = <String>[];
  String? hiddenTag;
  for (final rawLine in contents.split(RegExp(r'\r?\n'))) {
    final line = rawLine.trim();
    final lower = line.toLowerCase();
    if (hiddenTag != null) {
      if (lower == '</$hiddenTag>') hiddenTag = null;
      continue;
    }
    final block = RegExp(r'^<([^>]+)>$').firstMatch(lower);
    if (block != null) {
      final tag = block.group(1)!;
      if (sensitiveTags.contains(tag) || tag.contains('key')) {
        final label = tag == 'private-key' ? '私钥' : tag;
        result.add('<$tag>（$label内容已隐藏）');
        hiddenTag = tag;
      }
      continue;
    }
    if (line.isEmpty || line.startsWith('#') || line.startsWith(';')) continue;
    final command = line.split(RegExp(r'\s+')).first.toLowerCase();
    if (command == 'auth-user-pass') {
      result.add('auth-user-pass（凭据不展示）');
    } else if (visibleCommands.contains(command)) {
      result.add(line);
    }
  }
  return result;
}

String formatSessionTime(DateTime? value) {
  if (value == null) return '暂无记录';
  final local = value.toLocal();
  String twoDigits(int number) => number.toString().padLeft(2, '0');
  return '${local.year}-${twoDigits(local.month)}-${twoDigits(local.day)} '
      '${twoDigits(local.hour)}:${twoDigits(local.minute)}';
}
