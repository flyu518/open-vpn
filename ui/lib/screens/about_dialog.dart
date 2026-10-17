import 'dart:io';

import 'package:flutter/material.dart';

import '../services/update_service.dart';

class OpenVpnAboutDialog extends StatefulWidget {
  const OpenVpnAboutDialog({
    super.key,
    required this.version,
    required this.updateService,
    required this.platform,
    this.onOpenReleasePage,
  });

  final String version;
  final UpdateService updateService;
  final UpdatePlatform platform;
  final Future<void> Function(Uri uri)? onOpenReleasePage;

  @override
  State<OpenVpnAboutDialog> createState() => _OpenVpnAboutDialogState();
}

class _OpenVpnAboutDialogState extends State<OpenVpnAboutDialog> {
  UpdateCheckResult? _result;
  bool _checking = false;
  bool _installing = false;
  String? _error;

  Future<void> _checkForUpdates() async {
    setState(() {
      _checking = true;
      _error = null;
    });
    final result = await widget.updateService.checkForUpdates();
    if (!mounted) return;
    setState(() {
      _checking = false;
      _result = result;
    });
  }

  Future<void> _installUpdate() async {
    final release = _result?.release;
    if (release == null) return;
    final asset = release.installerFor(widget.platform);
    if (asset == null) {
      final opener = widget.onOpenReleasePage;
      if (opener == null) {
        setState(() => _error = '没有找到当前平台的安装包');
      } else {
        await opener(release.releaseUrl);
      }
      return;
    }

    setState(() {
      _installing = true;
      _error = null;
    });
    try {
      await widget.updateService.downloadAndLaunch(asset);
      if (!mounted) return;
      setState(() => _installing = false);
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('安装包已打开，请按系统提示完成更新')),
      );
    } on Object catch (error) {
      if (!mounted) return;
      setState(() {
        _installing = false;
        _error = '下载或打开安装包失败：$error';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final colors = Theme.of(context).colorScheme;
    final result = _result;
    final release = result?.release;
    final canInstall = result?.status == UpdateCheckStatus.available;
    final installer = release?.installerFor(widget.platform);
    return AlertDialog(
      title: const Row(
        children: [
          Icon(Icons.shield_outlined),
          SizedBox(width: 10),
          Text('关于 OpenVPN'),
        ],
      ),
      content: SizedBox(
        width: 500,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'OpenVPN 多连接桌面客户端',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 6),
            Text('当前版本 ${widget.version}'),
            const SizedBox(height: 18),
            if (result != null) ...[
              Container(
                width: double.infinity,
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: _resultColor(colors, result.status).withValues(
                    alpha: .08,
                  ),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
                  result.message,
                  style: TextStyle(color: _resultColor(colors, result.status)),
                ),
              ),
              if (release != null && canInstall) ...[
                const SizedBox(height: 12),
                Text(
                  release.name,
                  style: Theme.of(context).textTheme.titleSmall,
                ),
                if (release.description.isNotEmpty) ...[
                  const SizedBox(height: 5),
                  Text(
                    _shortDescription(release.description),
                    maxLines: 4,
                    overflow: TextOverflow.ellipsis,
                  ),
                ],
                const SizedBox(height: 5),
                Text(
                  installer == null ? '未找到当前平台安装包，将打开 Release 页面' : installer.name,
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: colors.onSurfaceVariant,
                      ),
                ),
              ],
            ],
            if (_error != null) ...[
              const SizedBox(height: 10),
              Text(_error!, style: TextStyle(color: colors.error)),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: _checking || _installing
              ? null
              : () => Navigator.of(context).pop(),
          child: const Text('关闭'),
        ),
        if (canInstall)
          FilledButton.icon(
            onPressed: _installing ? null : _installUpdate,
            icon: _installing
                ? const SizedBox.square(
                    dimension: 16,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : Icon(
                    installer == null
                        ? Icons.open_in_new_rounded
                        : Icons.download_rounded,
                  ),
            label: Text(
              _installing
                  ? '准备安装…'
                  : installer == null
                      ? '打开 Release 页面'
                      : '下载并安装',
            ),
          )
        else
          FilledButton.icon(
            onPressed: _checking ? null : _checkForUpdates,
            icon: _checking
                ? const SizedBox.square(
                    dimension: 16,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.refresh_rounded),
            label: Text(_checking ? '检查中…' : '检查更新'),
          ),
      ],
    );
  }

  Color _resultColor(ColorScheme colors, UpdateCheckStatus status) {
    return switch (status) {
      UpdateCheckStatus.available => colors.primary,
      UpdateCheckStatus.upToDate => const Color(0xFF287A5C),
      UpdateCheckStatus.notConfigured => colors.onSurfaceVariant,
      UpdateCheckStatus.error => colors.error,
    };
  }

  String _shortDescription(String value) {
    final normalized = value.replaceAll(RegExp(r'\s+'), ' ').trim();
    return normalized.length > 220
        ? '${normalized.substring(0, 217)}…'
        : normalized;
  }
}

Future<void> openReleasePage(Uri uri) async {
  if (Platform.isMacOS) {
    await Process.start('open', <String>[uri.toString()]);
    return;
  }
  if (Platform.isWindows) {
    await Process.start(
      'rundll32.exe',
      <String>['url.dll,FileProtocolHandler', uri.toString()],
    );
    return;
  }
  throw UnsupportedError('当前平台不支持打开 Release 页面');
}
