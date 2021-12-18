#pragma once

enum EventType {
	None,
	ProcessCreate,
	ProcessExit
};

struct ProcessEventInfo {
	EventType eventType;
	ULONG processId;
};