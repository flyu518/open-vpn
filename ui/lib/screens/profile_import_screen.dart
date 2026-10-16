import 'package:flutter/material.dart';

class ProfileImportScreen extends StatelessWidget {
  const ProfileImportScreen({super.key, this.onImport});

  final VoidCallback? onImport;

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('导入 VPN 配置'),
      content: const Text('选择 .ovpn 文件后，VPN 会出现在连接总览中。'),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('取消'),
        ),
        FilledButton(
          onPressed: onImport,
          child: const Text('选择文件'),
        ),
      ],
    );
  }
}
