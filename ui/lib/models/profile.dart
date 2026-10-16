class ImportedProfile {
  const ImportedProfile({
    required this.id,
    required this.displayName,
    required this.path,
    required this.contents,
  });

  factory ImportedProfile.fromFile({
    required String path,
    required String contents,
  }) {
    final normalizedPath = path.replaceAll('\\', '/');
    final fileName = normalizedPath.split('/').last;
    final displayName = fileName.toLowerCase().endsWith('.ovpn')
        ? fileName.substring(0, fileName.length - '.ovpn'.length)
        : fileName;
    return ImportedProfile(
      id: _stableProfileId(normalizedPath),
      displayName: displayName.isEmpty ? '未命名配置' : displayName,
      path: path,
      contents: contents,
    );
  }

  final String id;
  final String displayName;
  final String path;
  final String contents;
}

String _stableProfileId(String value) {
  var hash = 0x811c9dc5;
  for (final codeUnit in value.codeUnits) {
    hash ^= codeUnit;
    hash = (hash * 0x01000193) & 0xffffffff;
  }
  return 'profile-${hash.toRadixString(16).padLeft(8, '0')}';
}
