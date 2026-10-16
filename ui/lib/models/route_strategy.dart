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
