if(NOT DEFINED OPENVPN3_SOURCE_DIR OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "需要 OPENVPN3_SOURCE_DIR 和 OUTPUT_DIRECTORY")
endif()

# Keep the official NRPT implementation, but give this multi-session product
# its own rule prefix and a string owner (client PID + interface index).
# Never overwrite/delete rules belonging to official OpenVPN clients.
function(replace_required OLD NEW)
    string(FIND "${SOURCE}" "${OLD}" AT)
    if(AT EQUAL -1)
        message(FATAL_ERROR "Windows session DNS patch boundary missing: ${OLD}")
    endif()
    string(REPLACE "${OLD}" "${NEW}" SOURCE "${SOURCE}")
    set(SOURCE "${SOURCE}" PARENT_SCOPE)
endfunction()

file(READ "${OPENVPN3_SOURCE_DIR}/openvpn/tun/win/nrpt.hpp" SOURCE)
replace_required("class Nrpt" "class SessionNrpt")
replace_required("check_reg_error<nrpt_error>" "Win::check_reg_error<nrpt_error>")
replace_required("DWORD process_id" "std::string process_id")
replace_required("std::to_wstring(process_id)" "wstring::from_utf8(process_id)")
replace_required("if (process_id &&" "if (!process_id.empty() &&")
replace_required("OpenVPNDNSRouting" "OpenVPNMultiSessionDNSRouting")
replace_required("using NRPT = Nrpt<Win::Reg, Win::NetApi>;"
                 "using SessionNRPT = SessionNrpt<Win::Reg, Win::NetApi>;")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}/openvpn/tun/win/client")
file(WRITE "${OUTPUT_DIRECTORY}/openvpn/tun/win/session_nrpt.hpp" "${SOURCE}")

file(READ "${OPENVPN3_SOURCE_DIR}/openvpn/tun/win/client/tunsetup.hpp" SOURCE)
replace_required("#include <openvpn/tun/win/nrpt.hpp>"
                 "#include <openvpn/tun/win/nrpt.hpp>\n#include <openvpn/tun/win/session_nrpt.hpp>")
replace_required("DWORD pid = process_id_ ? process_id_ : ::GetCurrentProcessId();"
                 "const std::string pid = std::to_string(process_id_ ? process_id_ : ::GetCurrentProcessId()) + \"-\" + std::to_string(tap.index);")
replace_required("NRPT::ActionCreate" "SessionNRPT::ActionCreate")
replace_required("NRPT::ActionDelete" "SessionNRPT::ActionDelete")
replace_required([=[        if (pull.block_ipv6)
        {
            static const char *const block_ipv6_net[] = {
                "2000::/4",
                "3000::/4",
                "fc00::/7",
            };
            for (size_t i = 0; i < array_size(block_ipv6_net); ++i)
            {
                create.add(new WinCmd("netsh interface ipv6 add route " + std::string(block_ipv6_net[i]) + " interface=1 store=active"));
                destroy.add(new WinCmd("netsh interface ipv6 delete route " + std::string(block_ipv6_net[i]) + " interface=1 store=active"));
            }
        }]=]
        [=[        // Machine-wide IPv6 sink routes are leased by the multi-session
        // agent. Keep pull.block_ipv6 intact for all other IPv6 safeguards.]=])
file(WRITE "${OUTPUT_DIRECTORY}/openvpn/tun/win/client/tunsetup.hpp" "${SOURCE}")
