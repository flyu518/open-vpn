import 'package:flutter/material.dart';

import '../services/startup_service.dart';

class DesktopSettingsDialog extends StatefulWidget {
  const DesktopSettingsDialog({
    super.key,
    required this.startupService,
    this.onCheckForUpdates,
    this.onStartupChanged,
  });

  final StartupService startupService;
  final Future<void> Function()? onCheckForUpdates;
  final ValueChanged<bool>? onStartupChanged;

  @override
  State<DesktopSettingsDialog> createState() => _DesktopSettingsDialogState();
}

class _DesktopSettingsDialogState extends State<DesktopSettingsDialog> {
  bool _enabled = false;
  bool _loading = true;
  bool _saving = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _loadStartupState();
  }

  Future<void> _loadStartupState() async {
    try {
      final enabled = await widget.startupService.isEnabled();
      if (!mounted) return;
      setState(() {
        _enabled = enabled;
        _loading = false;
      });
    } on Object catch (error) {
      if (!mounted) return;
      setState(() {
        _loading = false;
        _error = '读取开机自启状态失败：$error';
      });
    }
  }

  Future<void> _setStartup(bool value) async {
    if (_saving) return;
    final previous = _enabled;
    setState(() {
      _enabled = value;
      _saving = true;
      _error = null;
    });
    try {
      if (value) {
        await widget.startupService.enable();
      } else {
        await widget.startupService.disable();
      }
      if (!mounted) return;
      widget.onStartupChanged?.call(value);
      setState(() => _saving = false);
    } on Object catch (error) {
      if (!mounted) return;
      setState(() {
        _enabled = previous;
        _saving = false;
        _error = '保存开机自启失败：$error';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Row(
        children: [
          Icon(Icons.settings_outlined),
          SizedBox(width: 10),
          Text('桌面设置'),
        ],
      ),
      content: SizedBox(
        width: 460,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            SwitchListTile.adaptive(
              contentPadding: EdgeInsets.zero,
              value: _enabled,
              onChanged: _loading || _saving ? null : _setStartup,
              title: const Text('开机自启'),
              subtitle: Text(
                _loading
                    ? '正在读取系统设置…'
                    : '登录系统后自动启动 OpenVPN，并保留在状态栏/托盘中',
              ),
            ),
            const Divider(height: 24),
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.system_update_alt_rounded),
              title: const Text('检查更新'),
              subtitle: const Text('从 GitLab Release 检查新的桌面安装包'),
              trailing: const Icon(Icons.chevron_right_rounded),
              onTap: widget.onCheckForUpdates,
            ),
            if (_error != null) ...[
              const SizedBox(height: 8),
              Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  _error!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('关闭'),
        ),
      ],
    );
  }
}
