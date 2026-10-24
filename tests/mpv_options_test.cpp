#include "core/MpvPlayer.h"
#include <mpv/client.h>
#include <cassert>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>

static std::string option(vw::MpvPlayer& player, const char* name, const char* key)
{
    mpv_node node{};
    assert(mpv_get_property(player.handle(), name, MPV_FORMAT_NODE, &node) >= 0);
    std::string result;
    assert(node.format == MPV_FORMAT_NODE_MAP);
    for (int i = 0; i < node.u.list->num; ++i)
        if (std::string(node.u.list->keys[i]) == key) {
            assert(node.u.list->values[i].format == MPV_FORMAT_STRING);
            result = node.u.list->values[i].u.string;
        }
    mpv_free_node_contents(&node);
    return result;
}

int main()
{
    const std::string extractor = "C:/a,b/tools/yt-dlp.exe";
    const std::string cookie = "C:/a,b/session cookies.txt";
    vw::MpvPlayer player({extractor, cookie, "bestvideo+bestaudio/best"});
    assert(player.lastError().empty());
    player.setOption("vo", "null");
    player.setOption("ao", "null");
    assert(player.initialize());
    assert(option(player, "options/script-opts", "ytdl_hook-ytdl_path") == extractor);
    assert(option(player, "options/ytdl-raw-options", "cookies") == cookie);

    // Both options must survive in the same map when the portable JS runtime
    // is present, including paths containing mpv's list separators.
    auto directory = std::filesystem::temp_directory_path() /
        ("videowallpaper-options," + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    auto deno = directory / "deno.exe";
    std::ofstream(deno).close();
    const auto extractorUtf8 = (directory / "yt-dlp.exe").generic_u8string();
    const auto denoUtf8 = deno.generic_u8string();
    {
        vw::MpvPlayer portable({std::string(reinterpret_cast<const char*>(extractorUtf8.data()), extractorUtf8.size()), cookie, ""});
        assert(portable.lastError().empty());
        portable.setOption("vo", "null");
        portable.setOption("ao", "null");
        assert(portable.initialize());
        assert(option(portable, "options/ytdl-raw-options", "cookies") == cookie);
        assert(option(portable, "options/ytdl-raw-options", "js-runtimes") ==
            "deno:" + std::string(reinterpret_cast<const char*>(denoUtf8.data()), denoUtf8.size()));
    }
    std::filesystem::remove(deno);
    std::filesystem::remove(directory);
}
