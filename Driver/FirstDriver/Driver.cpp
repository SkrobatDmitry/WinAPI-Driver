#include <ntddk.h>

#include "Const.h"
#include "LCWC.h"

void ProcessesLifeCycleWatcherUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS ProcessesLifeCycleWatcherCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS ProcessesLifeCycleWatcherRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0);

#define DRIVER_TAG 'gaTm'

Globals g_Globals;

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT  DriverObject, _In_ PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\ProcessesLifeCycleWatcher");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ProcessesLifeCycleWatcher");
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status;

	status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create device"));
		return status;
	}

	DeviceObject->Flags |= DO_BUFFERED_IO;

	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create link"));
		IoDeleteDevice(DeviceObject);
		return status;
	}

	status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to register callback"));
		IoDeleteSymbolicLink(&symLink);
		IoDeleteDevice(DeviceObject);
		return status;
	}

	InitializeListHead(&g_Globals.SearchListHead);
	InitializeListHead(&g_Globals.readListHead);
	ExInitializeFastMutex(&g_Globals.mutex);

	DriverObject->DriverUnload = ProcessesLifeCycleWatcherUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = ProcessesLifeCycleWatcherCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = ProcessesLifeCycleWatcherRead;

	KdPrint(("Driver init"));

	return STATUS_SUCCESS;
}

void ProcessesLifeCycleWatcherUnload(_In_ PDRIVER_OBJECT DriverObject) {
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ProcessesLifeCycleWatcher");

	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	KdPrint(("Driver unload"));
}

NTSTATUS ProcessesLifeCycleWatcherCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	return CompleteIrp(Irp, STATUS_SUCCESS, 0);
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
	UNREFERENCED_PARAMETER(Process);
	UNREFERENCED_PARAMETER(ProcessId);
	UNREFERENCED_PARAMETER(CreateInfo);

	if (CreateInfo != NULL) {

		UNICODE_STRING targetImage = RTL_CONSTANT_STRING(PROCESS_X);
		if ((CreateInfo->ImageFileName == NULL) || (RtlCompareUnicodeString(&targetImage, CreateInfo->ImageFileName, TRUE) != 0)) {
			return;
		}
		KdPrint(("Target process created"));

		ProcessEventItem* eItem = (ProcessEventItem*)ExAllocatePoolWithTag(PagedPool, sizeof(ProcessEventItem), DRIVER_TAG);
		if (eItem == NULL) {
			KdPrint(("Failed to allocate pool"));
			return;
		}

		eItem->eventInfo.eventType = ProcessCreate;
		eItem->eventInfo.processId = HandleToULong(ProcessId);
		eItem->linksCount = 2;

		ExAcquireFastMutex(&g_Globals.mutex);

		InsertTailList(&g_Globals.SearchListHead, &eItem->toSearchListEntry);
		InsertTailList(&g_Globals.readListHead, &eItem->readListEntry);

		ExReleaseFastMutex(&g_Globals.mutex);

		KdPrint(("Start event enqueued"));
	}
	else {

		ULONG ulongProcessId = HandleToULong(ProcessId);
		ProcessEventItem* killedProcess = NULL;

		bool needRelise = false;
		ExAcquireFastMutex(&g_Globals.mutex);

		PLIST_ENTRY next = g_Globals.SearchListHead.Flink;
		while (next != &g_Globals.SearchListHead) {

			ProcessEventItem* curr = CONTAINING_RECORD(next, ProcessEventItem, toSearchListEntry);
			if (curr->eventInfo.processId == ulongProcessId) {

				killedProcess = curr;

				RemoveEntryList(&killedProcess->toSearchListEntry);
				killedProcess->linksCount--;
				needRelise = killedProcess->linksCount == 0;

				break;
			}

			next = next->Flink;
		}

		ExReleaseFastMutex(&g_Globals.mutex);
		
		if (killedProcess == NULL) {
			return;
		}

		if (needRelise) {
			ExFreePool(killedProcess);
			KdPrint(("Event dequeued"));
		}

		ProcessEventItem* eItem = (ProcessEventItem*)ExAllocatePoolWithTag(PagedPool, sizeof(ProcessEventItem), DRIVER_TAG);
		if (eItem == NULL) {
			KdPrint(("Failed to allocate pool"));
			return;
		}

		eItem->eventInfo.eventType = ProcessExit;
		eItem->eventInfo.processId = ulongProcessId;
		eItem->linksCount = 1;
		
		
		ExAcquireFastMutex(&g_Globals.mutex);

		InsertTailList(&g_Globals.readListHead, &eItem->readListEntry);

		ExReleaseFastMutex(&g_Globals.mutex);
		KdPrint(("Exit event enqueued"));
	}
}

NTSTATUS ProcessesLifeCycleWatcherRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ULONG length = stack->Parameters.Read.Length;

	if (length < sizeof(ProcessEventInfo)) {
		return CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
	}

	ProcessEventInfo* sysbuffer = (ProcessEventInfo*)Irp->AssociatedIrp.SystemBuffer;
	ProcessEventItem* readingItem = NULL;

	bool needRelise = false;
	ExAcquireFastMutex(&g_Globals.mutex);

	if (g_Globals.readListHead.Flink != &g_Globals.readListHead) {
		readingItem = CONTAINING_RECORD(g_Globals.readListHead.Flink, ProcessEventItem, readListEntry);
		RemoveEntryList(&readingItem->readListEntry);
		readingItem->linksCount--;
		needRelise = readingItem->linksCount == 0;
	}

	ExReleaseFastMutex(&g_Globals.mutex);

	if (readingItem != NULL) {
		sysbuffer->eventType = readingItem->eventInfo.eventType;
		sysbuffer->processId = readingItem->eventInfo.processId;

		if (needRelise) {
			ExFreePool(readingItem);
			KdPrint(("Event dequeued"));
		}

		return CompleteIrp(Irp, STATUS_SUCCESS, sizeof(ProcessEventInfo));
	}
	
	return CompleteIrp(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR info) {
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}