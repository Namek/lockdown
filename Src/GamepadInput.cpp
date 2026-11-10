#include "GamepadInput.h"
#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

extern "C"
{
	#include <hidsdi.h>
}


namespace
{
	struct HatSwitch
	{
		HIDP_VALUE_CAPS Caps;
		unsigned Directions = 0;
	};

	struct Gamepad
	{
		std::vector<BYTE> PreparsedData;
		std::vector<USAGE_AND_PAGE> Usages;
		std::unordered_map<BYTE, std::vector<USAGE_AND_PAGE>> PressedButtons;
		std::vector<HatSwitch> Hats;
		DWORD DualSenseProduct = 0;
		USHORT ReportLength = 0;
		unsigned DualSenseButtons = 0;

		bool Initialize(HANDLE device);
		bool ButtonPressed(BYTE* report, ULONG length);
		bool DualSenseButtonPressed(const BYTE* report, ULONG length);
	};

	std::unordered_map<HANDLE, Gamepad> Devices;

	// HID hats encode a direction clockwise from up, with a separate value for neutral.
	unsigned HatDirections(ULONG position, ULONG positions)
	{
		constexpr unsigned Directions[] = { 1, 3, 2, 6, 4, 12, 8, 9 };
		if (positions == 4 && position < 4)
			return Directions[2 * position];
		if (positions == 8 && position < 8)
			return Directions[position];
		return 0;
	}

	bool Gamepad::Initialize(HANDLE device)
	{
		UINT size = 0;
		if (GetRawInputDeviceInfo(device, RIDI_PREPARSEDDATA, nullptr, &size) == UINT(-1) || !size)
			return false;

		PreparsedData.resize(size);
		if (GetRawInputDeviceInfo(device, RIDI_PREPARSEDDATA, PreparsedData.data(), &size) == UINT(-1))
			return false;

		auto preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(PreparsedData.data());
		HIDP_CAPS caps;
		if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS)
			return false;

		ReportLength = caps.InputReportByteLength;
		RID_DEVICE_INFO info{};
		info.cbSize = sizeof(info);
		size = sizeof(info);
		if (GetRawInputDeviceInfo(device, RIDI_DEVICEINFO, &info, &size) != UINT(-1) &&
			info.dwType == RIM_TYPEHID && info.hid.dwVendorId == 0x054C &&
			(info.hid.dwProductId == 0x0CE6 || info.hid.dwProductId == 0x0DF2))
		{
			DualSenseProduct = info.hid.dwProductId;
			return true;
		}

		Usages.resize(HidP_MaxUsageListLength(HidP_Input, 0, preparsed));
		USHORT hatCount = caps.NumberInputValueCaps;
		if (hatCount)
		{
			std::vector<HIDP_VALUE_CAPS> hats(hatCount);
			if (HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, HID_USAGE_GENERIC_HATSWITCH,
				hats.data(), &hatCount, preparsed) == HIDP_STATUS_SUCCESS)
			{
				for (USHORT i = 0; i < hatCount; i++)
					Hats.push_back({ hats[i] });
			}
		}
		return true;
	}

	bool Gamepad::DualSenseButtonPressed(const BYTE* report, ULONG length)
	{
		// Extended Bluetooth reports use vendor data instead of standard HID button usages.
		unsigned offset;
		unsigned lastByteMask = DualSenseProduct == 0x0DF2 ? 0xF7 : 0x07;
		if (report[0] == 0x31 && length >= 78)
			offset = 9;
		else if (report[0] == 0x01 && ReportLength == 64 && length >= 11)
			offset = 8;
		else if (report[0] == 0x01 && ReportLength != 64 && length >= 8)
		{
			offset = 5;
			lastByteMask = 0x03; // The other bits in this byte are a counter in Bluetooth basic mode.
		}
		else
			return false;

		unsigned buttons = HatDirections(report[offset] & 0x0F, 8) |
			(report[offset] & 0xF0) |
			(unsigned(report[offset + 1]) << 8) |
			((report[offset + 2] & lastByteMask) << 16);
		bool pressed = (buttons & ~DualSenseButtons) != 0;
		DualSenseButtons = buttons;
		return pressed;
	}

	bool Gamepad::ButtonPressed(BYTE* report, ULONG length)
	{
		if (DualSenseProduct)
			return DualSenseButtonPressed(report, length);

		auto preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(PreparsedData.data());
		auto data = reinterpret_cast<PCHAR>(report);
		bool pressed = false;
		ULONG usageCount = ULONG(Usages.size());
		if (usageCount && HidP_GetUsagesEx(HidP_Input, 0, Usages.data(), &usageCount, preparsed, data, length) == HIDP_STATUS_SUCCESS)
		{
			auto end = std::remove_if(Usages.begin(), Usages.begin() + usageCount, [](const USAGE_AND_PAGE& usage)
			{
				return usage.UsagePage != HID_USAGE_PAGE_BUTTON &&
					!(usage.UsagePage == HID_USAGE_PAGE_GENERIC &&
						usage.Usage >= HID_USAGE_GENERIC_DPAD_UP && usage.Usage <= HID_USAGE_GENERIC_DPAD_LEFT);
			});
			auto& previous = PressedButtons[report[0]];
			for (auto button = Usages.begin(); button != end; ++button)
			{
				if (std::none_of(previous.begin(), previous.end(), [&](const USAGE_AND_PAGE& old)
				{
					return old.UsagePage == button->UsagePage && old.Usage == button->Usage;
				}))
				{
					pressed = true;
					break;
				}
			}
			previous.assign(Usages.begin(), end);
		}

		for (auto& hat : Hats)
		{
			if (hat.Caps.ReportID != report[0])
				continue;

			ULONG value;
			if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, hat.Caps.LinkCollection,
				HID_USAGE_GENERIC_HATSWITCH, &value, preparsed, data, length) == HIDP_STATUS_SUCCESS)
			{
				unsigned directions = HatDirections(value - hat.Caps.LogicalMin, hat.Caps.LogicalMax - hat.Caps.LogicalMin + 1);
				pressed |= (directions & ~hat.Directions) != 0;
				hat.Directions = directions;
			}
		}
		return pressed;
	}
}


bool GamepadInput::Register(HWND window)
{
	constexpr DWORD Flags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
	const RAWINPUTDEVICE devices[] =
	{
		{ HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_JOYSTICK, Flags, window },
		{ HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_GAMEPAD, Flags, window }
	};
	return RegisterRawInputDevices(devices, ARRAYSIZE(devices), sizeof(RAWINPUTDEVICE)) != FALSE;
}


bool GamepadInput::ButtonPressed(HRAWINPUT input)
{
	static std::vector<BYTE> buffer;
	constexpr UINT DataOffset = offsetof(RAWINPUT, data.hid.bRawData);
	UINT size = 0;
	if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == UINT(-1) || size < DataOffset)
		return false;

	buffer.resize(size);
	UINT bytesRead = GetRawInputData(input, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER));
	if (bytesRead == UINT(-1) || bytesRead < DataOffset)
		return false;

	auto raw = reinterpret_cast<RAWINPUT*>(buffer.data());
	const auto& hid = raw->data.hid;
	if (raw->header.dwType != RIM_TYPEHID || !hid.dwSizeHid || hid.dwCount > (bytesRead - DataOffset) / hid.dwSizeHid)
		return false;

	HANDLE device = raw->header.hDevice;
	auto [gamepad, added] = Devices.try_emplace(device);
	if (added && !gamepad->second.Initialize(device))
	{
		Devices.erase(gamepad);
		return false;
	}

	bool pressed = false;
	BYTE* report = buffer.data() + DataOffset;
	for (DWORD i = 0; i < hid.dwCount; i++, report += hid.dwSizeHid)
		pressed |= gamepad->second.ButtonPressed(report, hid.dwSizeHid);
	return pressed;
}


void GamepadInput::RemoveDevice(HANDLE device)
{
	Devices.erase(device);
}
