#pragma once

#include <ntddk.h>

NTSTATUS RLoggerCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS RLoggerDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);