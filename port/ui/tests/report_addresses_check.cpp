// report_addresses.cpp on its own: which numbers in a report's text are hidden as internet addresses. Exit 0 when
// every case holds.
#include "report_addresses.hpp"

#include <cstdio>
#include <string>

int main() {
    struct Case {
        const char* in;
        const char* out;
    } cases[] = {
        // other players and hosts, as the logs name them
        {"[sceNpMatching2] signaling: member 2 (CoopB) at 203.0.113.9:3659",
         "[sceNpMatching2] signaling: member 2 (CoopB) at <address>:3659"},
        {"[psnr] connected to 198.51.100.23:36100 as \"x\"", "[psnr] connected to <address>:36100 as \"x\""},
        {"PSNR_SERVER=8.8.8.8:36100", "PSNR_SERVER=<address>:36100"},
        {"(1.1.1.1)", "(<address>)"},
        {"a 9.9.9.9, b 9.9.9.10.", "a <address>, b <address>."},
        {"8.8.8.8", "<address>"},
        // addresses that say nothing outside a home stay
        {"at 192.168.68.51:3659", "at 192.168.68.51:3659"},
        {"10.0.0.2 172.16.5.4 172.31.0.1 127.0.0.1 169.254.1.1 100.64.0.1 0.0.0.0 239.255.255.250",
         "10.0.0.2 172.16.5.4 172.31.0.1 127.0.0.1 169.254.1.1 100.64.0.1 0.0.0.0 239.255.255.250"},
        {"172.32.0.1 100.128.0.1", "<address> <address>"},  // (just outside those ranges)
        // version numbers stay
        {"Windows 10.0.26100.1", "Windows 10.0.26100.1"},
        {"driver 32.0.15.6094", "driver 32.0.15.6094"},
        {"v1.2.3.4 and 1.2.3.4.5 and 1.2.3", "v1.2.3.4 and 1.2.3.4.5 and 1.2.3"},
        {"build_1.2.3.4 x-1.2.3.4 a1.2.3.4", "build_1.2.3.4 x-1.2.3.4 a1.2.3.4"},
        {"1.2.3.4a 256.1.1.1 1.2.3.1000", "1.2.3.4a 256.1.1.1 1.2.3.1000"},
        {"no numbers", "no numbers"},
        {"", ""},
    };
    int bad = 0;
    for (const Case& c : cases) {
        const std::string got = ui::report::hide_addresses(c.in);
        if (got != c.out) {
            std::printf("FAIL: \"%s\"\n  got  \"%s\"\n  want \"%s\"\n", c.in, got.c_str(), c.out);
            bad++;
        }
    }
    std::printf("report_addresses_check: %s (%d cases)\n", bad ? "FAILED" : "all passed", int(sizeof cases / sizeof *cases));
    return bad ? 1 : 0;
}
