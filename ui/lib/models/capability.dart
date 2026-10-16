enum VpnCapability {
  desktopMulti,
  mobileSingle,
}

extension VpnCapabilityText on VpnCapability {
  int get maxActiveSessions {
    switch (this) {
      case VpnCapability.desktopMulti:
        return 0x7fffffff;
      case VpnCapability.mobileSingle:
        return 1;
    }
  }

  String get label {
    switch (this) {
      case VpnCapability.desktopMulti:
        return '桌面多连接';
      case VpnCapability.mobileSingle:
        return '单活动连接';
    }
  }
}
