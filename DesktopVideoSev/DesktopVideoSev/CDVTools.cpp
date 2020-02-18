#include "stdafx.h"
#include "CDVTools.h"

CDVTools::CDVTools()
{

}


CDVTools::~CDVTools()
{

}

HWND workw = 0;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	HWND child = FindWindowExA(hwnd, 0, "SHELLDLL_DefView", 0);
	if (child)
	{
		workw = FindWindowExA(0, hwnd, "WorkerW", 0);
		//workw = hwnd;
		if (workw)
		{
			return false;
		}
		return true;
	}
	return true;
}
HWND CDVTools::GetHandleBeforeDesktopIcon()
{
	HWND hwndProgman = ::FindWindowA("Progman", NULL);
	::SendMessage(hwndProgman, 0x052c, 0, 0);
	::EnumWindows(EnumWindowsProc, 0);
	return workw;
}
