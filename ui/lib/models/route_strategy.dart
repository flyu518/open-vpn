enum RouteConflictMode {
  compatible,
  strict,
  ask,
}

enum RouteRuleType {
  ip,
  cidr,
  domain,
  domainSuffix,
}

class RouteRule {
  const RouteRule({
    required this.type,
    required this.pattern,
    this.enabled = true,
  });

  final RouteRuleType type;
  final String pattern;
  final bool enabled;

  factory RouteRule.fromJson(Map<String, dynamic> value) {
    final typeName = value['type'] as String? ?? RouteRuleType.cidr.name;
    final type = RouteRuleType.values.firstWhere(
      (item) => item.name == typeName,
      orElse: () => RouteRuleType.cidr,
    );
    return RouteRule(
      type: type,
      pattern: value['pattern'] as String? ?? '',
      enabled: value['enabled'] as bool? ?? true,
    );
  }

  Map<String, Object?> toJson() => <String, Object?>{
        'type': type.name,
        'pattern': pattern,
        'enabled': enabled,
      };
}

class RouteStrategy {
  const RouteStrategy({
    this.conflictMode = RouteConflictMode.compatible,
    this.priority = 0,
    this.rules = const <RouteRule>[],
  });

  final RouteConflictMode conflictMode;
  final int priority;
  final List<RouteRule> rules;

  factory RouteStrategy.fromJson(Map<String, dynamic> value) {
    final modeName =
        value['conflictMode'] as String? ?? RouteConflictMode.compatible.name;
    final mode = RouteConflictMode.values.firstWhere(
      (item) => item.name == modeName,
      orElse: () => RouteConflictMode.compatible,
    );
    final rawRules = value['rules'];
    return RouteStrategy(
      conflictMode: mode,
      priority: (value['priority'] as num?)?.toInt() ?? 0,
      rules: rawRules is List
          ? rawRules
              .whereType<Map>()
              .map((item) => RouteRule.fromJson(
                    Map<String, dynamic>.from(item),
                  ))
              .toList(growable: false)
          : const <RouteRule>[],
    );
  }

  Map<String, Object?> toJson() => <String, Object?>{
        'conflictMode': conflictMode.name,
        'priority': priority,
        'rules': rules.map((rule) => rule.toJson()).toList(),
      };

  RouteStrategy copyWith({
    RouteConflictMode? conflictMode,
    int? priority,
    List<RouteRule>? rules,
  }) {
    return RouteStrategy(
      conflictMode: conflictMode ?? this.conflictMode,
      priority: priority ?? this.priority,
      rules: rules ?? this.rules,
    );
  }
}
