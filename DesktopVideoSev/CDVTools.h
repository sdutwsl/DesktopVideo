#pragma once
#include <windows.h>

class CDVTools
{
public:
	CDVTools();
	virtual ~CDVTools();
	static HWND GetHandleBeforeDesktopIcon();
};
