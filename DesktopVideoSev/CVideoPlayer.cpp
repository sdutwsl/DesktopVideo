#include "CVideoPlayer.h"

CVideoPlayer::CVideoPlayer() :pVLCInstance(0), pVLCMedia(0), pVLCPlayer(0)
{
	const char * const vlc_args[] = {
		"-I", "dumy",
		"--ignore-config",
		"--plugin-path=./plugins" };

	pVLCInstance = libvlc_new(0, 0);
	pVLCPlayer = libvlc_media_player_new(pVLCInstance);

}


CVideoPlayer::~CVideoPlayer()
{
	libvlc_media_player_release(pVLCPlayer);
	libvlc_release(pVLCInstance);
}

void CVideoPlayer::OpenMedia(const char *filePath)
{
	pVLCMedia = libvlc_media_new_path(pVLCInstance, filePath);
	libvlc_media_player_set_media(pVLCPlayer, pVLCMedia);
}

void CVideoPlayer::Play()
{
	libvlc_media_player_play(pVLCPlayer);
}

void CVideoPlayer::Pause()
{
	libvlc_media_player_pause(pVLCPlayer);
}

void CVideoPlayer::Stop()
{
	libvlc_media_player_stop(pVLCPlayer);
}

int64_t CVideoPlayer::GetLength()
{
	auto length = libvlc_media_player_get_length(pVLCPlayer);
	return length;
}

int64_t CVideoPlayer::GetTime()
{
	auto time = libvlc_media_player_get_time(pVLCPlayer);
	return time;
}

void CVideoPlayer::SetTime(const uint64_t time)
{
	libvlc_media_player_set_time(pVLCPlayer, time);
}

void CVideoPlayer::SetRate(const float rate)
{
	libvlc_media_player_set_rate(pVLCPlayer, rate);
}

float CVideoPlayer::GetRate()
{
	auto rate = libvlc_media_player_get_rate(pVLCPlayer);
	return rate;
}

void CVideoPlayer::SetVolume(const unsigned int volume)
{
	libvlc_audio_set_volume(pVLCPlayer, volume);
}

unsigned int CVideoPlayer::GetVolume()
{
	auto volume = libvlc_audio_get_volume(pVLCPlayer);
	return volume;
}

void CVideoPlayer::Mute(const bool mute)
{
	libvlc_audio_set_mute(pVLCPlayer, mute);
}

bool CVideoPlayer::GetMute()
{
	auto mute = libvlc_audio_get_mute(pVLCPlayer);
	return mute;
}

void CVideoPlayer::SetWindow(void *hwnd)
{
	libvlc_media_player_set_hwnd(pVLCPlayer, hwnd);
}
