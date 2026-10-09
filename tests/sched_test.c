// Scheduler/events/InTime behaviour test (no game data needed).
#include <stdio.h>
#include <string.h>

#include "../src/game/sched.h"
#include "../src/game/events.h"

static double fake_now;
double platform_now(void) { return fake_now; }

static char log_buf[4096];
static void logf_(const char *s) { strncat(log_buf, s, sizeof(log_buf) - strlen(log_buf) - 1); }

static void a_update(task_t *t) {
	char s[32];
	snprintf(s, sizeof(s), "A%d ", gGameUpdateTime);
	logf_(s);
}
static void b_update(task_t *t) {
	char s[32];
	snprintf(s, sizeof(s), "B%d ", gGameUpdateTime);
	logf_(s);
	if (gGameUpdateTime == 100) {
		LanderEvent ev = {5, {42, 7}};
		Event_Raise(&ev);
	}
}
static const task_type_t type_a = {0, a_update, NULL, NULL, NULL, NULL, NULL, 40};
static const task_type_t type_b = {0, b_update, NULL, NULL, NULL, NULL, NULL, 100};

static void on_event(const LanderEvent *ev) {
	char s[48];
	snprintf(s, sizeof(s), "E%d(%d,%d)@%d ", ev->type, (int)ev->arg[0], (int)ev->arg[1], gGameUpdateTime);
	logf_(s);
}
static void on_event_filtered(const LanderEvent *ev) { logf_("F "); }
static void later(intptr_t x, intptr_t y) {
	char s[32];
	snprintf(s, sizeof(s), "L%d,%d@%d ", (int)x, (int)y, gGameUpdateTime);
	logf_(s);
}

int main(void) {
	Sched_Reset();
	Events_Reset();
	EventType_Register(5, 2);
	EventType_AddHandler(5, NULL, on_event);
	intptr_t f[2] = {41, -1};
	EventType_AddHandler(5, f, on_event_filtered); // must not fire (param0 = 42)
	Task_CreateNow(&type_a, NULL);
	Task_CreateNow(&type_b, NULL);
	InTime2(130, later, 3, 4);
	Sched_RunUntil(200);
	printf("got:  %s\n", log_buf);
	return 0;
}
