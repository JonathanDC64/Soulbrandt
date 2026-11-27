// Written for Demon's Souls. A Mac's battery, from IOKit's list of power sources (a Mac has no /sys/class/power_supply),
// for setup_pc.cpp's platform_power: the setup screens' time estimate and the problem report's Power line. A file of
// its own, so CoreFoundation's names (MacTypes' Point, Rect and others) stay out of the others. Empty off a Mac.
#if defined(__APPLE__)

#include "setup_pc.hpp"

#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>

namespace ui::setup {

namespace {

bool same(CFTypeRef value, CFStringRef what) {
    return value && CFGetTypeID(value) == CFStringGetTypeID() &&
           CFStringCompare(static_cast<CFStringRef>(value), what, 0) == kCFCompareEqualTo;
}

int number(CFDictionaryRef d, CFStringRef key) {
    const CFTypeRef v = CFDictionaryGetValue(d, key);
    int n = 0;
    if (!v || CFGetTypeID(v) != CFNumberGetTypeID() ||
        !CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberIntType, &n))
        return -1;
    return n;
}

}  // namespace

// The Mac's own battery (an internal one: a pad's or a mouse's is not), whether it runs on it now, and its charge.
// A desktop Mac lists none.
Power mac_power() {
    Power out;
    const CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return out;
    if (const CFArrayRef list = IOPSCopyPowerSourcesList(info)) {
        for (CFIndex i = 0, n = CFArrayGetCount(list); i < n; i++) {
            const CFDictionaryRef d = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(list, i));
            if (!d || !same(CFDictionaryGetValue(d, CFSTR(kIOPSTypeKey)), CFSTR(kIOPSInternalBatteryType))) continue;
            if (!out.battery) {
                const int now = number(d, CFSTR(kIOPSCurrentCapacityKey)), top = number(d, CFSTR(kIOPSMaxCapacityKey));
                if (now >= 0 && top > 0) out.percent = now * 100 / top;
            }
            out.battery = true;
            out.onBattery = out.onBattery ||
                            same(CFDictionaryGetValue(d, CFSTR(kIOPSPowerSourceStateKey)), CFSTR(kIOPSBatteryPowerValue));
        }
        CFRelease(list);
    }
    CFRelease(info);
    return out;
}

}  // namespace ui::setup

#endif  // __APPLE__
