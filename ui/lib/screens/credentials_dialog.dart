import 'package:flutter/material.dart';

import '../models/credentials.dart';

class CredentialsDialog extends StatefulWidget {
  const CredentialsDialog({
    super.key,
    required this.profileName,
    this.initialUsername = '',
  });

  final String profileName;
  final String initialUsername;

  @override
  State<CredentialsDialog> createState() => _CredentialsDialogState();
}

class _CredentialsDialogState extends State<CredentialsDialog> {
  final _formKey = GlobalKey<FormState>();
  final _usernameController = TextEditingController();
  final _passwordController = TextEditingController();
  bool _remember = false;
  bool _useSaved = false;

  @override
  void initState() {
    super.initState();
    _usernameController.text = widget.initialUsername;
  }

  @override
  void dispose() {
    _usernameController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  void _submit() {
    if (_useSaved) {
      Navigator.of(context).pop(const VpnCredentials(
            username: '',
            password: '',
            useSaved: true,
          ));
      return;
    }
    if (!(_formKey.currentState?.validate() ?? false)) return;
    Navigator.of(context).pop(VpnCredentials(
      username: _usernameController.text.trim(),
      password: _passwordController.text,
      remember: _remember,
      useSaved: false,
    ));
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text('连接 ${widget.profileName}'),
      content: SizedBox(
        width: 420,
        child: Form(
          key: _formKey,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextFormField(
                controller: _usernameController,
                autofocus: true,
                enabled: !_useSaved,
                decoration: const InputDecoration(
                  labelText: '用户名',
                  prefixIcon: Icon(Icons.person_outline_rounded),
                ),
                validator: (value) =>
                    value == null || value.trim().isEmpty ? '请输入用户名' : null,
                onFieldSubmitted: (_) => _submit(),
              ),
              if (widget.initialUsername.isNotEmpty && !_useSaved)
                Align(
                  alignment: Alignment.centerLeft,
                  child: Padding(
                    padding: const EdgeInsets.only(top: 5, left: 12),
                    child: Text(
                      '已从配置文件读取，可按需修改',
                      style: Theme.of(context).textTheme.labelSmall?.copyWith(
                            color: Theme.of(context)
                                .colorScheme
                                .onSurfaceVariant,
                          ),
                    ),
                  ),
                ),
              const SizedBox(height: 12),
              CheckboxListTile(
                value: _useSaved,
                onChanged: (value) => setState(() => _useSaved = value ?? false),
                contentPadding: EdgeInsets.zero,
                title: const Text('使用已保存的凭据'),
                subtitle: const Text('从系统安全存储读取，不再次输入密码'),
                controlAffinity: ListTileControlAffinity.leading,
              ),
              if (!_useSaved) ...[
                TextFormField(
                  controller: _passwordController,
                  obscureText: true,
                  decoration: const InputDecoration(
                    labelText: '密码',
                    prefixIcon: Icon(Icons.lock_outline_rounded),
                  ),
                  validator: (value) =>
                      value == null || value.isEmpty ? '请输入密码' : null,
                  onFieldSubmitted: (_) => _submit(),
                ),
                const SizedBox(height: 8),
                CheckboxListTile(
                  value: _remember,
                  onChanged: (value) =>
                      setState(() => _remember = value ?? false),
                  contentPadding: EdgeInsets.zero,
                  title: const Text('保存到系统安全存储'),
                  subtitle:
                      const Text('只保存到 macOS 钥匙串或 Windows 凭据管理器'),
                  controlAffinity: ListTileControlAffinity.leading,
                ),
              ],
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('取消'),
        ),
        FilledButton.icon(
          onPressed: _submit,
          icon: const Icon(Icons.login_rounded),
          label: const Text('开始连接'),
        ),
      ],
    );
  }
}
