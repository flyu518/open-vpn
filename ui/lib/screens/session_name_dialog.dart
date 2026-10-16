import 'package:flutter/material.dart';

class SessionNameDialog extends StatefulWidget {
  const SessionNameDialog({super.key, required this.initialName});

  final String initialName;

  @override
  State<SessionNameDialog> createState() => _SessionNameDialogState();
}

class _SessionNameDialogState extends State<SessionNameDialog> {
  late final TextEditingController _controller;

  @override
  void initState() {
    super.initState();
    _controller = TextEditingController(text: widget.initialName);
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _save() {
    final value = _controller.text.trim();
    if (value.isEmpty) return;
    Navigator.of(context).pop(value);
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('编辑连接名称'),
      content: TextField(
        controller: _controller,
        autofocus: true,
        maxLength: 40,
        decoration: const InputDecoration(
          labelText: '显示名称',
          hintText: '例如：研发网络',
          prefixIcon: Icon(Icons.edit_outlined),
        ),
        onSubmitted: (_) => _save(),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('取消'),
        ),
        FilledButton(
          onPressed: _save,
          child: const Text('保存名称'),
        ),
      ],
    );
  }
}
