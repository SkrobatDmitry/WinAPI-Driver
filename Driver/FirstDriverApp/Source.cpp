#include <Windows.h>

#include <iostream>
#include <map>

#include "../FirstDriver/LCWCommon.h"
#include "../FirstDriver/Const.h"

int main() {

	HANDLE hFile = CreateFile(L"\\\\.\\ProcessesLifeCycleWatcher", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		std::cout << "Error: driver must be running!\n";
		std::cin.get();
		return 1;
	}

	std::map<long, HANDLE> processIdPairs;
	ProcessEventInfo eventInfo;

	while (true) {

		DWORD bytes;
		if (!ReadFile(hFile, &eventInfo, sizeof(ProcessEventInfo), &bytes, nullptr)) {
			break;
		}

		if (bytes == 0) {
			Sleep(500);
			continue;
		}

		if (bytes != sizeof(ProcessEventInfo)) {
			break;
		}


		if (eventInfo.eventType == ProcessCreate) {
			std::cout << "opening process id: " << eventInfo.processId << '\n';

			STARTUPINFO si = { sizeof(STARTUPINFO), 0 };
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi = { 0 };

			CreateProcessW(PROCESS_Y, NULL, 0, 0, 0, CREATE_DEFAULT_ERROR_MODE, 0, 0, &si, &pi);
			CloseHandle(pi.hThread);

			processIdPairs[eventInfo.processId] = pi.hProcess;

			continue;
		}

		if (eventInfo.eventType == ProcessExit) {
			std::cout << "terminating process id: " << eventInfo.processId << '\n';

			TerminateProcess(processIdPairs[eventInfo.processId], 1);
			CloseHandle(processIdPairs[eventInfo.processId]);

			continue;
		}

		break;
	}
	std::cout << "An error has occurred\n";

	CloseHandle(hFile);
	std::cin.get();
	return 0;
}