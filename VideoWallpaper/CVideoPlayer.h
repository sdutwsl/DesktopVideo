#pragma once

#include "vlc/vlc.h"
#include  "vlc/libvlc_events.h"

#pragma comment(lib,"libvlc.lib")
#pragma comment(lib,"libvlccore.lib")

class CVideoPlayer
{
private:
	libvlc_instance_t * pVLCInstance;
	libvlc_media_player_t *pVLCPlayer;
	libvlc_media_t *pVLCMedia;
public:
	//constructor & destructor
	CVideoPlayer();
	virtual ~CVideoPlayer();

	//media control
	void OpenMedia(const char *);

	//vedio control
	void Play();
	void Pause();
	void Stop();
	int64_t GetLength();
	int64_t GetTime();
	void SetTime(const uint64_t);
	void SetRate(const float);
	float GetRate();

	//audio control
	void SetVolume(const unsigned int);
	unsigned int GetVolume();
	void Mute(bool = true);
	bool GetMute();

	//end reach callback
	int SetEndCallback(void(*func)(const struct libvlc_event_t* p_event, void* p_data))
	{
		return libvlc_event_attach(libvlc_media_player_event_manager(pVLCPlayer), libvlc_MediaPlayerEndReached, func, 0);	
	}

	//hwnd
	void SetWindow(void *);
};