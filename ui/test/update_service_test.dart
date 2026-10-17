import 'package:flutter_test/flutter_test.dart';

import '../lib/services/update_service.dart';

void main() {
  group('SemanticVersion', () {
    test('比较两位数修订号时按数字而不是字符串排序', () {
      final current = SemanticVersion.parse('0.1.9');
      final latest = SemanticVersion.parse('v0.1.10');

      expect(latest.isNewerThan(current), isTrue);
      expect(current.isNewerThan(latest), isFalse);
    });

    test('拒绝不是三段数字的 GitLab tag', () {
      expect(SemanticVersion.tryParse('release-latest'), isNull);
      expect(SemanticVersion.tryParse('1.2'), isNull);
    });
  });

  test('解析 GitLab release 元数据和平台安装包', () {
    final release = GitLabRelease.fromJson(
      <String, Object?>{
        'tag_name': 'v0.2.0',
        'name': 'OpenVPN 0.2.0',
        'description': '修复桌面端问题',
        'assets': <String, Object?>{
          'links': <Object?>[
            <String, Object?>{
              'name': 'OpenVPN-0.2.0-windows.exe',
              'url': 'https://gitlab.example.com/windows.exe',
            },
            <String, Object?>{
              'name': 'OpenVPN-0.2.0-macos.pkg',
              'url': 'https://gitlab.example.com/macos.pkg',
            },
          ],
        },
      },
      releaseUrl: Uri.parse('https://gitlab.example.com/releases/v0.2.0'),
    );

    expect(release.version, SemanticVersion.parse('0.2.0'));
    expect(release.assets, hasLength(2));
    expect(
      release.installerFor(UpdatePlatform.macos)?.name,
      'OpenVPN-0.2.0-macos.pkg',
    );
    expect(
      release.installerFor(UpdatePlatform.windows)?.name,
      'OpenVPN-0.2.0-windows.exe',
    );
  });

  test('未配置 GitLab 项目时不发起请求并返回中文提示', () async {
    var requestCount = 0;
    final service = UpdateService(
      apiBaseUrl: '',
      projectPath: '',
      currentVersion: '0.1.0',
      responseLoader: (_) async {
        requestCount++;
        return const UpdateHttpResponse(200, '{}');
      },
    );

    final result = await service.checkForUpdates();

    expect(result.status, UpdateCheckStatus.notConfigured);
    expect(result.message, contains('未配置'));
    expect(requestCount, 0);
  });

  test('latest release 高于当前版本时报告有新版本', () async {
    final service = UpdateService(
      apiBaseUrl: 'https://gitlab.example.com/api/v4',
      projectPath: 'group/open-vpn',
      currentVersion: '0.1.9',
      responseLoader: (_) async => const UpdateHttpResponse(
        200,
        '{"tag_name":"v0.1.10","name":"OpenVPN 0.1.10","description":"更新"}',
      ),
    );

    final result = await service.checkForUpdates();

    expect(result.status, UpdateCheckStatus.available);
    expect(result.release?.version, SemanticVersion.parse('0.1.10'));
    expect(result.message, contains('0.1.10'));
  });

  test('GitLab 返回错误状态时返回可读错误', () async {
    final service = UpdateService(
      apiBaseUrl: 'https://gitlab.example.com/api/v4',
      projectPath: 'group/open-vpn',
      responseLoader: (_) async =>
          const UpdateHttpResponse(503, '{"message":"暂不可用"}'),
    );

    final result = await service.checkForUpdates();

    expect(result.status, UpdateCheckStatus.error);
    expect(result.message, contains('503'));
  });
}
