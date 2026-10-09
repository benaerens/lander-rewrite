// Events (port of 0x442c60-0x442d70 and the Event task 0x442ae0-0x442c20) and
// InTime delayed calls (0x456770-0x456980). The public API (EventType_*,
// Event_Raise, InTime, LanderEvent) is declared in mission_api.h.
#ifndef EVENTS_H
#define EVENTS_H

#include "mission_api.h"
#include "sched.h"

void Events_Reset(void);           // 0x442c60
void Events_RegisterAll(void);     // 0x4493e0: the engine's own event ids and parameter counts
extern bool gEventTrace;           // print raised events (debugging)

#endif
