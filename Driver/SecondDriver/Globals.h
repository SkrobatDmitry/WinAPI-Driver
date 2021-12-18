#pragma once

#include <ntddk.h>

struct Globals {
	BOOLEAN isLoggingActive;
	ULONG targetPID;
	LARGE_INTEGER regCookie;
};