#include <Windows.h>
#include <iostream>

#include "../SecondDriver/RLoggerCommon.h"

int main(int argc, char* argv[]) {

	HANDLE hDevice = CreateFile(L"\\\\.\\RLogger", FILE_SHARE_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE) {
		std::cout << "Error: driver must be running!\n";
		return 1;
	}

	if ((argc == 3) && (strcmp(argv[1], "setpid") == 0)) {
		DWORD returned;
		ULONG pid = atoi(argv[2]);

		if (DeviceIoControl(hDevice, IOCTL_RLOGGER_SET_TARGET_PID, &pid, sizeof(pid), nullptr, 0, &returned, nullptr)) {
			std::cout << "Success\n";
			return 0;
		}
		else {
			std::cout << "An error has occurred\n";
			return 1;
		}
	}

	if ((argc == 2) && (strcmp(argv[1], "stop") == 0)) {
		DWORD returned;

		if (DeviceIoControl(hDevice, IOCTL_RLOGGER_STOP_LOGGING, nullptr, 0, nullptr, 0, &returned, nullptr)) {
			std::cout << "Success\n";
			return 0;
		}
		else {
			std::cout << "An error has occurred\n";
			return 1;
		}
	}

	std::cout << "Error: use example:\n" << "./APPNAME setpid PID\n" << "./APPNAME stop\n";
	return 1;
}