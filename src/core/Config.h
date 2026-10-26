#pragma once

#include <map>
#include <string>

namespace muralop {

// Flat key=value settings file. Keys the core uses:
//   video                 default video for every monitor
//   video.<monitor id>    per-monitor override
//   volume                0..100
//   mute                  0/1
//   pause_when_locked     0/1
//   audio_monitor         which monitor plays audio (empty = platform's primary)
class Config {
public:
    explicit Config(std::string path, std::string legacyPath = {});

    bool load();
    bool save() const;

    std::string get(const std::string& key, const std::string& fallback = "") const;
    int getInt(const std::string& key, int fallback) const;
    bool getBool(const std::string& key, bool fallback) const;

    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, int value);
    void set(const std::string& key, bool value);
    void erase(const std::string& key);

    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::string legacyPath_;
    std::map<std::string, std::string> values_;
};

} // namespace muralop
