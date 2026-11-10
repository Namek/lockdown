#pragma once

#include <windows.h>

namespace GamepadInput
{
	bool Register(HWND window);
	bool ButtonPressed(HRAWINPUT input);
	void RemoveDevice(HANDLE device);
}
