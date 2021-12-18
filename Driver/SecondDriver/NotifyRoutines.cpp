#include "NotifyRoutines.h"

#include <stdio.h>

#include "Globals.h"

extern Globals g_Globals;

NTSTATUS OnRegistryNotify(PVOID context, PVOID arg1, PVOID arg2) {
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(arg2);

	if ((REG_NOTIFY_CLASS)(ULONG_PTR)arg1 != RegNtPostSetValueKey) {
		return STATUS_SUCCESS;
	}

	if (!g_Globals.isLoggingActive) {
		KdPrint(("Logging disabled"));

		return STATUS_SUCCESS;
	}

	ULONG pid = HandleToULong(PsGetCurrentProcessId());
	if ((pid != g_Globals.targetPID)) {
		KdPrint(("Uninteresting process, id: %u", pid));

		return STATUS_SUCCESS;
	}

	REG_POST_OPERATION_INFORMATION* info = (REG_POST_OPERATION_INFORMATION*)arg2;
	if (!NT_SUCCESS(info->Status)) {
		return STATUS_SUCCESS;
	}

	UNICODE_STRING fileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\RLoggerLog.txt");
	OBJECT_ATTRIBUTES objAttr;

	InitializeObjectAttributes(&objAttr, &fileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	HANDLE hFile;
	IO_STATUS_BLOCK ioStatusBlock;

	NTSTATUS status = ZwCreateFile(
		&hFile, 
		FILE_APPEND_DATA,
		&objAttr, 
		&ioStatusBlock, 
		0,
		FILE_ATTRIBUTE_NORMAL, 
		FILE_SHARE_READ | FILE_SHARE_WRITE, 
		FILE_OPEN_IF, 
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
		NULL, 
		0
	);

	if (!NT_SUCCESS(status)) {
		KdPrint(("Unable to open file"));

		return STATUS_SUCCESS;
	}

	REG_SET_VALUE_KEY_INFORMATION* vkInfo = (REG_SET_VALUE_KEY_INFORMATION*)info->PreInformation;
	if (vkInfo == NULL) {
		return STATUS_SUCCESS;
	}

	CHAR buffer[512];

	LARGE_INTEGER sysTime;
	KeQuerySystemTime(&sysTime);

	PCUNICODE_STRING name;
	CmCallbackGetKeyObjectIDEx(&g_Globals.regCookie, info->Object, NULL, &name, 0);
	
	int pos = sprintf_s(buffer, sizeof(buffer), "System time: %u\nPID: %u\n%ws\\\n%ws\nSize: %d\nData:",
		(ULONG)sysTime.QuadPart, pid, name->Buffer, vkInfo->ValueName->Buffer, vkInfo->DataSize);

	CmCallbackReleaseKeyObjectIDEx(name);

	int left = sizeof(buffer) - pos - ((int)strlen("\n\n") + 1);
	int limit = left < (int)vkInfo->DataSize ? left : (int)vkInfo->DataSize;
	for (int i = 0; i < limit; i++) {
		sprintf(buffer + pos + i, "%02X", ((UCHAR*)vkInfo->Data)[i]);
	}
	sprintf(buffer + pos + limit, "\n\n");

	LARGE_INTEGER EndOfFile;
	EndOfFile.HighPart = 0xffffffff;
	EndOfFile.LowPart = FILE_WRITE_TO_END_OF_FILE;

	status = ZwWriteFile(
		hFile, 
		NULL, 
		NULL, 
		NULL, 
		&ioStatusBlock, 
		(PVOID)buffer,
		(ULONG)strlen(buffer), 
		&EndOfFile, 
		NULL
	);

	if (!NT_SUCCESS(status)) {
		KdPrint(("Unable to write to file"));

		ZwClose(hFile);

		return STATUS_SUCCESS;
	}

	ZwClose(hFile);

	KdPrint(("Changes logged"));

	return STATUS_SUCCESS;
}