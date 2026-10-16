import 'package:flutter/material.dart';

import '../models/route_strategy.dart';

class RouteStrategyDialog extends StatefulWidget {
  const RouteStrategyDialog({
    super.key,
    required this.initial,
  });

  final RouteStrategy initial;

  @override
  State<RouteStrategyDialog> createState() => _RouteStrategyDialogState();
}

class _RouteStrategyDialogState extends State<RouteStrategyDialog> {
  late RouteConflictMode _mode;
  late int _priority;
  late RouteRuleType _ruleType;
  late final TextEditingController _patternController;
  late List<RouteRule> _rules;

  @override
  void initState() {
    super.initState();
    _mode = widget.initial.conflictMode;
    _priority = widget.initial.priority;
    _ruleType = RouteRuleType.cidr;
    _patternController = TextEditingController();
    _rules = List<RouteRule>.from(widget.initial.rules);
  }

  @override
  void dispose() {
    _patternController.dispose();
    super.dispose();
  }

  String _modeLabel(RouteConflictMode mode) {
    switch (mode) {
      case RouteConflictMode.compatible:
        return '兼容模式（前一个优先）';
      case RouteConflictMode.strict:
        return '严格模式（冲突即拒绝）';
      case RouteConflictMode.ask:
        return '询问模式（交给用户决定）';
    }
  }

  String _ruleTypeLabel(RouteRuleType type) {
    switch (type) {
      case RouteRuleType.ip:
        return 'IP 地址';
      case RouteRuleType.cidr:
        return 'CIDR 网段';
      case RouteRuleType.domain:
        return '精确域名';
      case RouteRuleType.domainSuffix:
        return '域名后缀/通配';
    }
  }

  void _addRule() {
    final pattern = _patternController.text.trim();
    if (pattern.isEmpty) return;
    setState(() {
      _rules = <RouteRule>[
        ..._rules,
        RouteRule(type: _ruleType, pattern: pattern),
      ];
      _patternController.clear();
    });
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('路由策略'),
      content: SizedBox(
        width: 520,
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              DropdownButtonFormField<RouteConflictMode>(
                initialValue: _mode,
                decoration: const InputDecoration(labelText: '冲突处理'),
                items: RouteConflictMode.values
                    .map((mode) => DropdownMenuItem(
                          value: mode,
                          child: Text(_modeLabel(mode)),
                        ))
                    .toList(),
                onChanged: (value) {
                  if (value != null) setState(() => _mode = value);
                },
              ),
              const SizedBox(height: 12),
              TextFormField(
                initialValue: _priority.toString(),
                decoration: const InputDecoration(
                  labelText: '连接优先级',
                  helperText: '数值越大，发生冲突时越优先保留该连接。',
                ),
                keyboardType: TextInputType.number,
                onChanged: (value) {
                  _priority = int.tryParse(value) ?? 0;
                },
              ),
              const SizedBox(height: 18),
              Text('匹配规则', style: Theme.of(context).textTheme.titleSmall),
              const SizedBox(height: 6),
              const Text(
                'IP/CIDR 是实际路由依据；域名规则会先解析成 IP。不要使用任意模糊或正则匹配。',
                style: TextStyle(fontSize: 12),
              ),
              const SizedBox(height: 10),
              Row(
                children: [
                  Expanded(
                    child: DropdownButtonFormField<RouteRuleType>(
                      initialValue: _ruleType,
                      decoration: const InputDecoration(labelText: '类型'),
                      items: RouteRuleType.values
                          .map((type) => DropdownMenuItem(
                                value: type,
                                child: Text(_ruleTypeLabel(type)),
                              ))
                          .toList(),
                      onChanged: (value) {
                        if (value != null) setState(() => _ruleType = value);
                      },
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    flex: 2,
                    child: TextField(
                      controller: _patternController,
                      decoration: const InputDecoration(
                        labelText: '例如 10.20.0.0/16',
                      ),
                      onSubmitted: (_) => _addRule(),
                    ),
                  ),
                  const SizedBox(width: 6),
                  IconButton(
                    tooltip: '添加规则',
                    onPressed: _addRule,
                    icon: const Icon(Icons.add_circle_outline_rounded),
                  ),
                ],
              ),
              if (_rules.isNotEmpty) ...[
                const SizedBox(height: 12),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: _rules.asMap().entries.map((entry) {
                    final index = entry.key;
                    final rule = entry.value;
                    return InputChip(
                      label: Text('${_ruleTypeLabel(rule.type)}：${rule.pattern}'),
                      onDeleted: () => setState(() => _rules.removeAt(index)),
                    );
                  }).toList(),
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
        FilledButton(
          onPressed: () => Navigator.of(context).pop(
            RouteStrategy(
              conflictMode: _mode,
              priority: _priority,
              rules: List<RouteRule>.unmodifiable(_rules),
            ),
          ),
          child: const Text('保存策略'),
        ),
      ],
    );
  }
}
