import 'package:flutter/material.dart';

import '../models/session.dart';

class ProfileLibraryDialog extends StatelessWidget {
  const ProfileLibraryDialog({
    super.key,
    required this.sessions,
    this.onImport,
  });

  final List<VpnSession> sessions;
  final VoidCallback? onImport;

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    return AlertDialog(
      title: const Text('配置文件'),
      content: SizedBox(
        width: 560,
        height: 360,
        child: sessions.isEmpty
            ? const _EmptyProfiles()
            : ListView.separated(
                itemCount: sessions.length,
                separatorBuilder: (_, __) => const SizedBox(height: 8),
                itemBuilder: (context, index) {
                  final session = sessions[index];
                  final loaded = session.profile.trim().isNotEmpty;
                  return Container(
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color:
                          colors.surfaceContainerHighest.withValues(alpha: .55),
                      borderRadius: BorderRadius.circular(14),
                      border: Border.all(color: colors.outlineVariant),
                    ),
                    child: Row(
                      children: [
                        Icon(
                          loaded
                              ? Icons.description_rounded
                              : Icons.description_outlined,
                          color:
                              loaded ? colors.primary : colors.onSurfaceVariant,
                        ),
                        const SizedBox(width: 12),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(session.name,
                                  style:
                                      Theme.of(context).textTheme.titleSmall),
                              const SizedBox(height: 3),
                              Text(
                                loaded
                                    ? '已加载配置 · ${session.status.label}'
                                    : '需要重新导入配置',
                                style: Theme.of(context).textTheme.bodySmall,
                              ),
                            ],
                          ),
                        ),
                        Icon(
                          loaded
                              ? Icons.check_circle_rounded
                              : Icons.info_outline_rounded,
                          size: 18,
                          color:
                              loaded ? Colors.greenAccent : Colors.orangeAccent,
                        ),
                      ],
                    ),
                  );
                },
              ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('关闭'),
        ),
        FilledButton.icon(
          onPressed: () {
            Navigator.of(context).pop();
            onImport?.call();
          },
          icon: const Icon(Icons.upload_file_rounded),
          label: const Text('添加配置'),
        ),
      ],
    );
  }
}

class _EmptyProfiles extends StatelessWidget {
  const _EmptyProfiles();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.folder_open_rounded,
              size: 46, color: Theme.of(context).colorScheme.primary),
          const SizedBox(height: 12),
          const Text('还没有配置文件'),
          const SizedBox(height: 6),
          const Text('导入 .ovpn 文件后会显示在这里。'),
        ],
      ),
    );
  }
}
