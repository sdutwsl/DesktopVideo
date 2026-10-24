#include "Config.h"

#include <filesystem>
#include <fstream>

namespace vw {

static std::string trim(const std::string& s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

Config::Config(std::string path) : path_(std::move(path)) {}

bool Config::load()
{
    std::ifstream in(std::filesystem::u8path(path_));
    if (!in)
        return false;
    values_.clear();
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        values_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}

bool Config::save() const
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path_).parent_path(), ec);
    std::ofstream out(std::filesystem::u8path(path_), std::ios::trunc);
    if (!out)
        return false;
    for (const auto& [key, value] : values_)
        out << key << '=' << value << '\n';
    return bool(out);
}

std::string Config::get(const std::string& key, const std::string& fallback) const
{
    auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

int Config::getInt(const std::string& key, int fallback) const
{
    auto it = values_.find(key);
    if (it == values_.end())
        return fallback;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

bool Config::getBool(const std::string& key, bool fallback) const
{
    auto it = values_.find(key);
    if (it == values_.end())
        return fallback;
    return it->second == "1" || it->second == "true" || it->second == "yes";
}

void Config::set(const std::string& key, const std::string& value) { values_[key] = value; }
void Config::set(const std::string& key, int value) { values_[key] = std::to_string(value); }
void Config::set(const std::string& key, bool value) { values_[key] = value ? "1" : "0"; }
void Config::erase(const std::string& key) { values_.erase(key); }

} // namespace vw
