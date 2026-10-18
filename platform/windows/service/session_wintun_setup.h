#pragma once

#include <openvpn/time/asiotimer.hpp>
#include <openvpn/tun/win/client/tunsetup.hpp>

#include "wintun_adapter_selection.h"

namespace vpn::windows {

class SessionWintunSetup final : public openvpn::TunWin::Setup {
  public:
    SessionWintunSetup(openvpn_io::io_context &io_context,
                      bool allow_local_dns_resolvers,
                      const std::wstring &adapter_name)
        : openvpn::TunWin::Setup(io_context, openvpn::TunWin::Wintun,
                                 allow_local_dns_resolvers),
          adapter_name_(openvpn::wstring::to_utf8(adapter_name)) {}

    HANDLE get_handle(std::ostream &os) override {
        if (get_adapter_state().index_defined())
            return INVALID_HANDLE_VALUE;

        openvpn::TunWin::Util::TapNameGuidPairList adapters(openvpn::TunWin::Wintun);
        retain_session_wintun_adapter(adapters, adapter_name_);
        openvpn::TunWin::Util::TapNameGuidPair tap;
        std::string path;
        openvpn::Win::ScopedHANDLE handle(openvpn::TunWin::Util::tap_open(
            openvpn::TunWin::Wintun, adapters, path, tap));
        if (!handle.defined())
            throw std::runtime_error("无法打开当前会话的 Wintun 网卡：" + adapter_name_);
        set_adapter_state(tap);
        os << "Session Wintun adapter: " << tap.name
           << " interface=" << tap.index << " SUCCEEDED\n";
        return handle.release();
    }

  private:
    std::string adapter_name_;
};

} // namespace vpn::windows
