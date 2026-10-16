import 'dart:async';

import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';

import '../models/profile.dart';

class ProfileImportScreen extends StatefulWidget {
  const ProfileImportScreen({super.key, this.onImport});

  final FutureOr<void> Function(ImportedProfile profile)? onImport;

  @override
  State<ProfileImportScreen> createState() => _ProfileImportScreenState();
}

class _ProfileImportScreenState extends State<ProfileImportScreen> {
  bool _loading = false;
  String? _error;

  Future<void> _chooseProfile() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      const typeGroup = XTypeGroup(
        label: 'OpenVPN 配置',
        extensions: <String>['ovpn', 'conf'],
      );
      final file = await openFile(
        acceptedTypeGroups: const <XTypeGroup>[typeGroup],
      );
      if (file == null) return;

      final contents = await file.readAsString();
      if (contents.trim().isEmpty) {
        throw const FormatException('配置文件为空');
      }
      await widget.onImport?.call(
        ImportedProfile.fromFile(path: file.path, contents: contents),
      );
      if (mounted) Navigator.of(context).pop();
    } on Object catch (error) {
      if (!mounted) return;
      setState(() => _error = '读取配置失败：$error');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('导入 OpenVPN 配置'),
      content: SizedBox(
        width: 460,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('选择 .ovpn 或 .conf 文件后，配置会出现在连接总览中。'),
            const SizedBox(height: 12),
            const Text(
              '配置内容只在当前进程中使用，不会写入应用日志。连接密码会在连接时单独询问。',
              style: TextStyle(fontSize: 12),
            ),
            if (_error != null) ...[
              const SizedBox(height: 14),
              Text(
                _error!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: _loading ? null : () => Navigator.of(context).pop(),
          child: const Text('取消'),
        ),
        FilledButton.icon(
          onPressed: _loading ? null : _chooseProfile,
          icon: _loading
              ? const SizedBox(
                  width: 16,
                  height: 16,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Icon(Icons.folder_open_rounded),
          label: Text(_loading ? '读取中…' : '选择文件'),
        ),
      ],
    );
  }
}
