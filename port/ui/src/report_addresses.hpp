// Written for Demon's Souls. Internet addresses out of a report (report.cpp's redact): an online log names other
// players' addresses ("signaling: member 2 (name) at <address>:3659") and the server or co-op host it reached, and a
// report is often posted in public, on an issue.
#pragma once

#include <string>

namespace ui::report {

// Each public IPv4 address in text as "<address>" (a ":port" after it stays). Addresses that say nothing outside a
// home stay: private (10/8, 172.16/12, 192.168/16), the provider's shared range (100.64/10), loopback, link-local,
// 0/8, multicast and above. So do version numbers: four parts, but one over 255 (10.0.26100.1), a fifth part
// (1.2.3.4.5), or a letter or a dot just before (v1.2.3.4).
std::string hide_addresses(std::string text);

}  // namespace ui::report
