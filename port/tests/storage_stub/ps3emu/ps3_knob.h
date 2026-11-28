#pragma once
#include <map>
#include <string>

inline std::map<std::string, std::string>& storage_environment() {
    static std::map<std::string, std::string> environment;
    return environment;
}
inline int ps3_env_int(const char* name, int fallback) {
    const auto found = storage_environment().find(name);
    return found == storage_environment().end() ? fallback : std::stoi(found->second);
}
inline const char* ps3_env_str(const char* name, const char* fallback = "") {
    const auto found = storage_environment().find(name);
    return found == storage_environment().end() ? fallback : found->second.c_str();
}
#define PS3_KNOB(value, expression) ((value) = (expression))
