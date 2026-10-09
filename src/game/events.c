#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "events.h"

#define MAX_HANDLERS 63

typedef struct handler_t {
	struct handler_t *next;
	EventHandlerFn fn;
	intptr_t filter[4];
} handler_t;

static int8_t param_count[EVENT_MAX_TYPES];
static handler_t *handlers[EVENT_MAX_TYPES];
static handler_t handler_pool[MAX_HANDLERS];
static handler_t *handler_free;
static handler_t *dispatch_next; // DAT_00551110: lets a handler remove the next one safely

void Events_Reset(void) { // 0x442c60
	memset(param_count, -1, sizeof(param_count));
	memset(handlers, 0, sizeof(handlers));
	for (int i = 0; i < MAX_HANDLERS; i++) {
		handler_pool[i].next = i + 1 < MAX_HANDLERS ? &handler_pool[i + 1] : NULL;
	}
	handler_free = &handler_pool[0];
	dispatch_next = NULL;
}

void Events_RegisterAll(void) { // 0x4493e0 (parameter counts, mission-api.md §3)
	static const int8_t counts[] = {1, 2, 0, 0, 2, 2, 2, 2, 3, 4, 3, 2, 2, 2, 3, 2, 3, 3, 1, 3, 1};
	for (int i = 0; i < (int)sizeof(counts); i++) {
		param_count[i] = counts[i];
	}
}

void EventType_Register(int32_t type, int32_t num_params) { // 0x442ca0
	param_count[type] = (int8_t)num_params;
}

void EventType_AddHandler(int32_t type, const intptr_t *filter, EventHandlerFn fn) { // 0x442cb0
	handler_t *h = handler_free;
	if (!h) {
		return; // the original has no exhaustion check
	}
	handler_free = h->next;
	h->fn = fn;
	for (int i = 0; i < param_count[type] && i < 4; i++) {
		h->filter[i] = filter ? filter[i] : -1;
	}
	h->next = handlers[type]; // newest first
	handlers[type] = h;
}

void EventType_RemoveHandler(int32_t type, EventHandlerFn fn) { // 0x442d20
	handler_t **link = &handlers[type];
	while (*link && (*link)->fn != fn) {
		link = &(*link)->next;
	}
	handler_t *h = *link;
	if (!h) {
		return;
	}
	if (h == dispatch_next) {
		dispatch_next = h->next;
	}
	*link = h->next;
	h->next = handler_free;
	handler_free = h;
}

static void dispatch(const LanderEvent *ev) { // 0x442ae0
	handler_t *h = handlers[ev->type];
	while (h) {
		handler_t *next = h->next;
		dispatch_next = next;
		bool match = true;
		for (int i = 0; i < param_count[ev->type] && i < 4; i++) {
			if (h->filter[i] != -1 && (int32_t)h->filter[i] != (int32_t)ev->arg[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			h->fn(ev);
			next = dispatch_next;
		}
		h = next;
	}
	dispatch_next = NULL;
}

// ---------------------------------------------------------------- event task (0x4988b8)

_Static_assert(sizeof(LanderEvent) <= sizeof(((task_t *)0)->data), "event fits in task data");

static void event_init(task_t *t, void *arg) { // 0x442b50
	const LanderEvent *ev = arg;
	LanderEvent copy = {ev->type, {0}};
	for (int i = 0; i < param_count[ev->type] && i < EVENT_MAX_PARAMS; i++) {
		copy.arg[i] = ev->arg[i];
	}
	memcpy(t->data, &copy, sizeof(copy));
}

static void event_update(task_t *t) { // 0x442b90
	LanderEvent ev;
	memcpy(&ev, t->data, sizeof(ev));
	dispatch(&ev);
	Task_Kill(t);
}

static const task_type_t event_task = {0, event_update, NULL, event_init, NULL, NULL, NULL, 0};

bool gEventTrace; // LANDER_DEBUG: print the script-relevant events

void Event_Raise(const LanderEvent *ev) { // 0x442d70
	if (gEventTrace && ev->type != EV_TICK_1S && ev->type != EV_TICK_100MS) {
		printf("event %02x {%08x %08x %d} at %d\n", ev->type, (uint32_t)ev->arg[0], (uint32_t)ev->arg[1], (int)ev->arg[2],
			gGameUpdateTime);
	}
	Task_CreateNow(&event_task, (void *)ev);
}

// ---------------------------------------------------------------- InTime (0x4991e0)

typedef struct {
	InTimeFn fn;
	int32_t n;
	intptr_t args[6];
} intime_t;

_Static_assert(sizeof(intime_t) <= sizeof(((task_t *)0)->data), "intime fits in task data");

typedef void (*fn0)(void);
typedef void (*fn1)(intptr_t);
typedef void (*fn2)(intptr_t, intptr_t);
typedef void (*fn3)(intptr_t, intptr_t, intptr_t);
typedef void (*fn4)(intptr_t, intptr_t, intptr_t, intptr_t);
typedef void (*fn5)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef void (*fn6)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);

static void intime_update(task_t *t) { // 0x456770
	intime_t c;
	memcpy(&c, t->data, sizeof(c));
	intptr_t *a = c.args;
	switch (c.n) {
		case 0: ((fn0)c.fn)(); break;
		case 1: ((fn1)c.fn)(a[0]); break;
		case 2: ((fn2)c.fn)(a[0], a[1]); break;
		case 3: ((fn3)c.fn)(a[0], a[1], a[2]); break;
		case 4: ((fn4)c.fn)(a[0], a[1], a[2], a[3]); break;
		case 5: ((fn5)c.fn)(a[0], a[1], a[2], a[3], a[4]); break;
		case 6: ((fn6)c.fn)(a[0], a[1], a[2], a[3], a[4], a[5]); break;
		default: break; // n > 6: killed without a call
	}
	Task_Kill(t);
}

static const task_type_t intime_task = {0, intime_update, NULL, NULL, NULL, NULL, NULL, 0};

void InTime(int32_t delay, InTimeFn fn, int32_t n, ...) { // 0x456980
	task_t *t = Task_Create(&intime_task, NULL, delay);
	if (!t) {
		return;
	}
	intime_t c = {fn, n, {0}};
	va_list ap;
	va_start(ap, n);
	for (int i = 0; i < n && i < 6; i++) {
		c.args[i] = va_arg(ap, intptr_t);
	}
	va_end(ap);
	memcpy(t->data, &c, sizeof(c));
}
