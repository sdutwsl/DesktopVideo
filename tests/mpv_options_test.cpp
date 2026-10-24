#include "core/MpvPlayer.h"
#include <mpv/client.h>
#include <cassert>
#include <string>

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
}
