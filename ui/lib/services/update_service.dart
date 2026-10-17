import 'dart:convert';
import 'dart:io';

const String buildAppVersion = String.fromEnvironment(
  'OPENVPN_VERSION',
  defaultValue: '0.1.0',
);
const String buildGitLabApi = String.fromEnvironment('OPENVPN_GITLAB_API');
const String buildGitLabProject = String.fromEnvironment(
  'OPENVPN_GITLAB_PROJECT',
);

enum UpdatePlatform { macos, windows }

enum UpdateCheckStatus { notConfigured, upToDate, available, error }

class SemanticVersion implements Comparable<SemanticVersion> {
  SemanticVersion(this.major, this.minor, this.patch);

  final int major;
  final int minor;
  final int patch;

  static final RegExp _pattern = RegExp(r'^v?(\d+)\.(\d+)\.(\d+)$');

  factory SemanticVersion.parse(String value) {
    final parsed = tryParse(value);
    if (parsed == null) {
      throw FormatException('版本号必须是三段数字：$value');
    }
    return parsed;
  }

  static SemanticVersion? tryParse(String value) {
    final match = _pattern.firstMatch(value.trim());
    if (match == null) return null;
    return SemanticVersion(
      int.parse(match.group(1)!),
      int.parse(match.group(2)!),
      int.parse(match.group(3)!),
    );
  }

  bool isNewerThan(SemanticVersion other) => compareTo(other) > 0;

  @override
  int compareTo(SemanticVersion other) {
    final majorResult = major.compareTo(other.major);
    if (majorResult != 0) return majorResult;
    final minorResult = minor.compareTo(other.minor);
    if (minorResult != 0) return minorResult;
    return patch.compareTo(other.patch);
  }

  @override
  bool operator ==(Object other) {
    return other is SemanticVersion && compareTo(other) == 0;
  }

  @override
  int get hashCode => Object.hash(major, minor, patch);

  @override
  String toString() => '$major.$minor.$patch';
}

class GitLabReleaseAsset {
  const GitLabReleaseAsset({required this.name, required this.url});

  final String name;
  final Uri url;

  factory GitLabReleaseAsset.fromJson(Map<String, Object?> json) {
    final name = json['name'];
    final url = json['url'];
    if (name is! String || name.trim().isEmpty || url is! String) {
      throw const FormatException('GitLab Release 资源缺少名称或下载地址');
    }
    final parsedUrl = Uri.tryParse(url);
    if (parsedUrl == null || !parsedUrl.hasScheme) {
      throw FormatException('GitLab Release 资源地址无效：$url');
    }
    return GitLabReleaseAsset(name: name.trim(), url: parsedUrl);
  }
}

class GitLabRelease {
  GitLabRelease({
    required this.tagName,
    required this.version,
    required this.name,
    required this.description,
    required this.assets,
    required this.releaseUrl,
  });

  final String tagName;
  final SemanticVersion version;
  final String name;
  final String description;
  final List<GitLabReleaseAsset> assets;
  final Uri releaseUrl;

  factory GitLabRelease.fromJson(
    Map<String, Object?> json, {
    required Uri releaseUrl,
  }) {
    final tagName = json['tag_name'];
    if (tagName is! String || tagName.trim().isEmpty) {
      throw const FormatException('GitLab Release 缺少 tag_name');
    }
    final version = SemanticVersion.tryParse(tagName);
    if (version == null) {
      throw FormatException('GitLab Release tag 不是三段版本号：$tagName');
    }

    final rawAssets = (json['assets'] as Map?)?['links'];
    final assets = <GitLabReleaseAsset>[];
    if (rawAssets is List) {
      for (final item in rawAssets) {
        if (item is Map) {
          try {
            assets.add(
              GitLabReleaseAsset.fromJson(
                Map<String, Object?>.from(item),
              ),
            );
          } on FormatException {
            // 单个异常资源不应该阻断整个 Release 的版本检查。
          }
        }
      }
    }

    return GitLabRelease(
      tagName: tagName.trim(),
      version: version,
      name: (json['name'] as String?)?.trim().isNotEmpty == true
          ? (json['name'] as String).trim()
          : tagName.trim(),
      description: (json['description'] as String?)?.trim() ?? '',
      assets: List<GitLabReleaseAsset>.unmodifiable(assets),
      releaseUrl: releaseUrl,
    );
  }

  GitLabReleaseAsset? installerFor(UpdatePlatform platform) {
    final candidates = assets.where((asset) {
      final lowerName = asset.name.toLowerCase();
      return switch (platform) {
        UpdatePlatform.macos => lowerName.endsWith('.pkg'),
        UpdatePlatform.windows =>
          lowerName.endsWith('.exe') || lowerName.endsWith('.msi'),
      };
    }).toList();
    if (candidates.isEmpty) return null;

    final platformHints = switch (platform) {
      UpdatePlatform.macos => const <String>['mac', 'darwin', 'osx'],
      UpdatePlatform.windows => const <String>['windows', 'win'],
    };
    candidates.sort((left, right) {
      final leftScore = _platformScore(left.name, platformHints);
      final rightScore = _platformScore(right.name, platformHints);
      return rightScore.compareTo(leftScore);
    });
    return candidates.first;
  }

  static int _platformScore(String name, List<String> hints) {
    final lowerName = name.toLowerCase();
    return hints.any(lowerName.contains) ? 1 : 0;
  }
}

class UpdateHttpResponse {
  const UpdateHttpResponse(this.statusCode, this.body);

  final int statusCode;
  final String body;
}

typedef UpdateResponseLoader = Future<UpdateHttpResponse> Function(Uri uri);
typedef UpdateFileDownloader = Future<File> Function(Uri uri, String fileName);
typedef UpdateInstallerLauncher = Future<void> Function(File file);

class UpdateCheckResult {
  const UpdateCheckResult({
    required this.status,
    required this.currentVersion,
    required this.message,
    this.release,
  });

  final UpdateCheckStatus status;
  final SemanticVersion currentVersion;
  final String message;
  final GitLabRelease? release;
}

class UpdateService {
  UpdateService({
    String? apiBaseUrl,
    String? projectPath,
    String? currentVersion,
    UpdateResponseLoader? responseLoader,
    UpdateFileDownloader? fileDownloader,
    UpdateInstallerLauncher? installerLauncher,
  })  : apiBaseUrl = (apiBaseUrl ?? buildGitLabApi).trim(),
        projectPath = (projectPath ?? buildGitLabProject).trim(),
        currentVersion = SemanticVersion.parse(
          (currentVersion ?? buildAppVersion).trim(),
        ),
        _responseLoader = responseLoader,
        _fileDownloader = fileDownloader,
        _installerLauncher = installerLauncher;

  final String apiBaseUrl;
  final String projectPath;
  final SemanticVersion currentVersion;
  final UpdateResponseLoader? _responseLoader;
  final UpdateFileDownloader? _fileDownloader;
  final UpdateInstallerLauncher? _installerLauncher;

  Future<UpdateCheckResult> checkForUpdates() async {
    if (apiBaseUrl.isEmpty || projectPath.isEmpty) {
      return UpdateCheckResult(
        status: UpdateCheckStatus.notConfigured,
        currentVersion: currentVersion,
        message: '当前构建未配置 GitLab 更新地址',
      );
    }

    final uri = _latestReleaseUri();
    try {
      final response = await (_responseLoader ?? _loadResponse)(uri);
      if (response.statusCode < 200 || response.statusCode >= 300) {
        return UpdateCheckResult(
          status: UpdateCheckStatus.error,
          currentVersion: currentVersion,
          message: '检查更新失败（HTTP ${response.statusCode}）',
        );
      }
      final decoded = jsonDecode(response.body);
      if (decoded is! Map) {
        throw const FormatException('GitLab 返回内容不是对象');
      }
      final release = GitLabRelease.fromJson(
        Map<String, Object?>.from(decoded),
        releaseUrl: _releasePageUri(decoded),
      );
      if (release.version.isNewerThan(currentVersion)) {
        return UpdateCheckResult(
          status: UpdateCheckStatus.available,
          currentVersion: currentVersion,
          release: release,
          message: '发现新版本 ${release.version}',
        );
      }
      return UpdateCheckResult(
        status: UpdateCheckStatus.upToDate,
        currentVersion: currentVersion,
        release: release,
        message: '当前已是最新版本（$currentVersion）',
      );
    } on Object catch (error) {
      return UpdateCheckResult(
        status: UpdateCheckStatus.error,
        currentVersion: currentVersion,
        message: '检查更新失败：${_shortError(error)}',
      );
    }
  }

  Future<File> downloadAndLaunch(
    GitLabReleaseAsset asset, {
    String? fileName,
  }) async {
    final file = await (_fileDownloader ?? _downloadFile)(
      asset.url,
      fileName ?? _safeFileName(asset.name),
    );
    await (_installerLauncher ?? _launchInstaller)(file);
    return file;
  }

  Uri _latestReleaseUri() {
    final base = apiBaseUrl.endsWith('/')
        ? apiBaseUrl.substring(0, apiBaseUrl.length - 1)
        : apiBaseUrl;
    return Uri.parse(
      '$base/projects/${Uri.encodeComponent(projectPath)}/releases/permalink/latest',
    );
  }

  Uri _releasePageUri(Map<dynamic, dynamic> json) {
    final webUrl = json['web_url'];
    if (webUrl is String) {
      final parsed = Uri.tryParse(webUrl);
      if (parsed != null && parsed.hasScheme) return parsed;
    }
    final base = Uri.parse(apiBaseUrl);
    final root = '${base.scheme}://${base.authority}';
    return Uri.parse('$root/$projectPath/-/releases/${json['tag_name']}');
  }

  Future<UpdateHttpResponse> _loadResponse(Uri uri) async {
    final client = HttpClient();
    try {
      final request = await client.getUrl(uri);
      request.headers.set(HttpHeaders.acceptHeader, 'application/json');
      final response = await request.close();
      return UpdateHttpResponse(
        response.statusCode,
        await response.transform(utf8.decoder).join(),
      );
    } finally {
      client.close(force: true);
    }
  }

  Future<File> _downloadFile(Uri uri, String fileName) async {
    final client = HttpClient();
    final directory = await Directory.systemTemp.createTemp('openvpn-update-');
    final file = File('${directory.path}/$fileName');
    try {
      final request = await client.getUrl(uri);
      final response = await request.close();
      if (response.statusCode < 200 || response.statusCode >= 300) {
        throw HttpException('下载更新失败（HTTP ${response.statusCode}）');
      }
      final sink = file.openWrite();
      try {
        await response.pipe(sink);
      } finally {
        await sink.close();
      }
      return file;
    } finally {
      client.close(force: true);
    }
  }

  Future<void> _launchInstaller(File file) async {
    if (Platform.isMacOS) {
      await Process.start('open', <String>[file.path]);
      return;
    }
    if (Platform.isWindows) {
      await Process.start(file.path, const <String>[]);
      return;
    }
    throw UnsupportedError('当前平台不支持启动 OpenVPN 更新安装包');
  }

  String _safeFileName(String value) {
    final safe = value.replaceAll(RegExp(r'[\\/:*?"<>|]'), '_').trim();
    return safe.isEmpty ? 'openvpn-update' : safe;
  }

  String _shortError(Object error) {
    final text = error.toString().replaceFirst(RegExp(r'^Exception: '), '').trim();
    return text.length > 160 ? '${text.substring(0, 157)}…' : text;
  }
}
