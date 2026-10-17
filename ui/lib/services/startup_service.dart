import 'dart:io';

enum DesktopPlatform { macos, windows, unsupported }

typedef StartupCommandRunner = Future<ProcessResult> Function(
  String executable,
  List<String> arguments,
);
typedef StartupUserIdLoader = Future<String> Function();

class StartupException implements Exception {
  const StartupException(this.message);

  final String message;

  @override
  String toString() => message;
}

class SingleInstanceGuard {
  SingleInstanceGuard({String? lockPath})
      : lockPath = lockPath ?? _defaultLockPath();

  final String lockPath;
  RandomAccessFile? _lockFile;
  static final Set<String> _heldPaths = <String>{};

  bool acquire() {
    if (_lockFile != null) return true;
    if (_heldPaths.contains(lockPath)) return false;
    try {
      final file = File(lockPath);
      file.parent.createSync(recursive: true);
      final handle = file.openSync(mode: FileMode.write);
      try {
        handle.lockSync(FileLock.exclusive);
      } on Object {
        handle.closeSync();
        return false;
      }
      _lockFile = handle;
      _heldPaths.add(lockPath);
      return true;
    } on Object {
      return false;
    }
  }

  void release() {
    final handle = _lockFile;
    _lockFile = null;
    if (handle == null) return;
    _heldPaths.remove(lockPath);
    try {
      handle.unlockSync();
    } finally {
      handle.closeSync();
    }
  }

  static String _defaultLockPath() {
    if (Platform.isWindows) {
      final localAppData = Platform.environment['LOCALAPPDATA'] ??
          Platform.environment['USERPROFILE'] ??
          Directory.systemTemp.path;
      return '$localAppData/OpenVPN/openvpn-ui.lock';
    }
    final home = Platform.environment['HOME'] ?? Directory.systemTemp.path;
    return '$home/Library/Application Support/OpenVPN/openvpn-ui.lock';
  }
}

class StartupService {
  StartupService({
    DesktopPlatform? platform,
    String? executablePath,
    String? homeDirectory,
    StartupCommandRunner? commandRunner,
    StartupUserIdLoader? userIdLoader,
  })  : platform = platform ?? _detectPlatform(),
        executablePath = executablePath ?? Platform.resolvedExecutable,
        homeDirectory = homeDirectory ?? _detectHomeDirectory(),
        _commandRunner = commandRunner,
        _userIdLoader = userIdLoader;

  static const String launchAgentLabel = 'com.openvpn.ui';
  static const String windowsRunKey =
      r'HKCU\Software\Microsoft\Windows\CurrentVersion\Run';

  final DesktopPlatform platform;
  final String executablePath;
  final String homeDirectory;
  final StartupCommandRunner? _commandRunner;
  final StartupUserIdLoader? _userIdLoader;

  String get launchAgentPath =>
      '$homeDirectory/Library/LaunchAgents/$launchAgentLabel.plist';

  String get macOsPlist => '''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$launchAgentLabel</string>
  <key>ProgramArguments</key>
  <array>
    <string>${_xmlEscape(executablePath)}</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
</dict>
</plist>
''';

  List<String> get windowsEnableArguments => <String>[
        'add',
        windowsRunKey,
        '/v',
        'OpenVPN',
        '/t',
        'REG_SZ',
        '/d',
        '"$executablePath"',
        '/f',
      ];

  List<String> get windowsDisableArguments => <String>[
        'delete',
        windowsRunKey,
        '/v',
        'OpenVPN',
        '/f',
      ];

  Future<bool> isEnabled() async {
    switch (platform) {
      case DesktopPlatform.macos:
        if (!await File(launchAgentPath).exists()) return false;
        final userId = await _userId();
        final result = await _run(
          'launchctl',
          <String>['print', 'gui/$userId/$launchAgentLabel'],
        );
        return result.exitCode == 0;
      case DesktopPlatform.windows:
        final result = await _run(
          'reg.exe',
          <String>['query', windowsRunKey, '/v', 'OpenVPN'],
        );
        return result.exitCode == 0;
      case DesktopPlatform.unsupported:
        return false;
    }
  }

  Future<void> enable() async {
    switch (platform) {
      case DesktopPlatform.macos:
        final file = File(launchAgentPath);
        await file.parent.create(recursive: true);
        await file.writeAsString(macOsPlist, flush: true);
        final userId = await _userId();
        await _runIgnoringFailure(
          'launchctl',
          <String>['bootout', 'gui/$userId/$launchAgentLabel'],
        );
        await _runChecked(
          'launchctl',
          <String>['bootstrap', 'gui/$userId', launchAgentPath],
          '加载 macOS 开机自启失败',
        );
      case DesktopPlatform.windows:
        await _runChecked(
          'reg.exe',
          windowsEnableArguments,
          '写入 Windows 开机自启失败',
        );
      case DesktopPlatform.unsupported:
        throw const StartupException('当前平台不支持开机自启');
    }
  }

  Future<void> disable() async {
    switch (platform) {
      case DesktopPlatform.macos:
        final userId = await _userId();
        final file = File(launchAgentPath);
        if (await file.exists()) await file.delete();
        await _runIgnoringFailure(
          'launchctl',
          <String>['bootout', 'gui/$userId/$launchAgentLabel'],
        );
      case DesktopPlatform.windows:
        await _runIgnoringFailure('reg.exe', windowsDisableArguments);
      case DesktopPlatform.unsupported:
        return;
    }
  }

  Future<ProcessResult> _run(String executable, List<String> arguments) {
    final runner = _commandRunner;
    if (runner != null) return runner(executable, arguments);
    return Process.run(executable, arguments);
  }

  Future<void> _runChecked(
    String executable,
    List<String> arguments,
    String message,
  ) async {
    final result = await _run(executable, arguments);
    if (result.exitCode != 0) {
      throw StartupException('$message：${_resultMessage(result)}');
    }
  }

  Future<void> _runIgnoringFailure(
    String executable,
    List<String> arguments,
  ) async {
    final result = await _run(executable, arguments);
    if (result.exitCode == 0) return;
    final output = '${result.stdout}\n${result.stderr}'.toLowerCase();
    if (output.contains('not found') ||
        output.contains('no such process') ||
        output.contains('could not find service') ||
        output.contains('unable to find the specified registry key or value')) {
      return;
    }
    throw StartupException('系统自启命令执行失败：${_resultMessage(result)}');
  }

  Future<String> _userId() async {
    final loader = _userIdLoader;
    if (loader != null) return loader();
    final result = await _run('id', const <String>['-u']);
    if (result.exitCode != 0) {
      throw StartupException('读取 macOS 用户 ID 失败：${_resultMessage(result)}');
    }
    final userId = result.stdout.toString().trim();
    if (userId.isEmpty) throw const StartupException('读取 macOS 用户 ID 失败');
    return userId;
  }

  String _resultMessage(ProcessResult result) {
    final message = result.stderr.toString().trim();
    return message.isEmpty ? '系统命令退出码 ${result.exitCode}' : message;
  }

  static DesktopPlatform _detectPlatform() {
    if (Platform.isMacOS) return DesktopPlatform.macos;
    if (Platform.isWindows) return DesktopPlatform.windows;
    return DesktopPlatform.unsupported;
  }

  static String _detectHomeDirectory() {
    if (Platform.isWindows) {
      return Platform.environment['USERPROFILE'] ??
          Platform.environment['APPDATA'] ??
          '';
    }
    return Platform.environment['HOME'] ?? '';
  }

  static String _xmlEscape(String value) {
    return value
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&apos;');
  }
}
