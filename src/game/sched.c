// Port of the original clock and scheduler. Addresses are in the D3D build.
#include <string.h>

#include "sched.h"
#include "../platform.h"

int32_t gGameUpdateTime;
int32_t gLevelEndFlag;
bool gSchedStall;
task_t *gCameraTask;
uint32_t gTimeScale = 0x1000;

static task_t pool[MAX_TASKS];
static task_t *task_list, *free_list;
static int num_tasks;

// ---------------------------------------------------------------- clock (0x4422e0-0x442410)

static uint32_t now_ms;      // game time (gNow)
static uint32_t clock_base;  // raw ms offset (DAT_0054aec4)
static uint32_t clock_game;  // DAT_0054aec0

static uint32_t raw_ms(void) {
	return (uint32_t)(platform_now() * 1000.0);
}

void Clock_Reset(void) {
	now_ms = 0;
	clock_game = 0;
	clock_base = raw_ms();
}

void Clock_Set(uint32_t ms) { // 0x4423d0
	now_ms = ms;
	clock_game = ms;
	clock_base = raw_ms() - ms;
}

uint32_t Clock_Update(void) { // 0x442410
	uint32_t elapsed = (raw_ms() - clock_game) - clock_base;
	uint32_t scaled = (uint32_t)(((uint64_t)elapsed * gTimeScale) >> 12);
	if (elapsed > 5000) {
		clock_base = raw_ms() - clock_game; // skip long gaps (Clock_Resync)
		return now_ms = clock_game;
	}
	clock_game += scaled;
	clock_base += elapsed - scaled;
	return now_ms = clock_game;
}

uint32_t Clock_Now(void) { return now_ms; }

// ---------------------------------------------------------------- tasks

static void insert_sorted(task_t *t) { // 0x442500: FIFO among equal wake times
	task_t **link = &task_list;
	while (*link && (*link)->wake <= t->wake) {
		link = &(*link)->next;
	}
	t->next = *link;
	*link = t;
}

static void pop_head_free(void) { // 0x442530
	task_t *t = task_list;
	task_list = t->next;
	t->next = free_list;
	free_list = t;
	num_tasks--;
}

void Sched_Reset(void) { // 0x442560
	Clock_Reset();
	for (int i = 0; i < MAX_TASKS; i++) {
		pool[i].type = NULL;
		pool[i].next = i + 1 < MAX_TASKS ? &pool[i + 1] : NULL;
	}
	task_list = NULL;
	free_list = &pool[0];
	gGameUpdateTime = (int32_t)now_ms;
	num_tasks = 0;
	gLevelEndFlag = 0;
	gCameraTask = NULL;
}

int Task_ToIndex(const task_t *t) { return (int)(t - pool); }
task_t *Task_FromIndex(int index) { return &pool[index]; }
task_t *Sched_GetHead(void) { return task_list; }
int Sched_FreeCount(void) { return MAX_TASKS - num_tasks; }

task_t *Task_Create(const task_type_t *type, void *arg, int32_t delay) { // 0x442670
	task_t *t = free_list;
	if (!t) {
		return NULL;
	}
	num_tasks++;
	free_list = t->next;
	t->wake = (uint32_t)gGameUpdateTime + (uint32_t)delay;
	t->type = type;
	memset(t->data, 0, sizeof(t->data));
	if (type->init) {
		type->init(t, arg);
	}
	insert_sorted(t);
	return t;
}

task_t *Task_CreateNow(const task_type_t *type, void *arg) {
	return Task_Create(type, arg, 0);
}

void Task_Kill(task_t *t) { // 0x4426e0: the slot is freed when it reaches the list head
	if (t->type && t->type->destroy) {
		t->type->destroy(t);
	}
	t->type = NULL;
}

void Task_SetWakeTime(task_t *t, uint32_t wake) { // 0x4428a0
	task_t **link = &task_list;
	while (*link != t) {
		link = &(*link)->next;
	}
	*link = t->next;
	uint32_t old = t->wake;
	t->wake = wake;
	if (old < wake) { // later: continue the search from where it was
		while (*link && (*link)->wake <= wake) {
			link = &(*link)->next;
		}
		t->next = *link;
		*link = t;
		return;
	}
	insert_sorted(t);
}

// 0x442700. Kept literally: after an update, whichever task is now at the head
// is re-scheduled (normally the one that just ran).
void Sched_RunUntil(uint32_t until) {
	gSchedStall = false;
	while (task_list && !task_list->type) {
		pop_head_free();
	}
	int32_t prev = gGameUpdateTime;
	task_t *t = task_list;
	while (t) {
		gGameUpdateTime = (int32_t)t->wake;
		if (until < (uint32_t)gGameUpdateTime || gLevelEndFlag) {
			gGameUpdateTime = prev;
			return;
		}
		if (t->type->update) {
			t->type->update(t);
			t = task_list;
		}
		if (gSchedStall) {
			return;
		}
		if (t->type) {
			task_list = t->next;
			if ((uint32_t)gGameUpdateTime == t->wake) {
				t->wake += t->type->period;
			}
			insert_sorted(t);
			t = task_list;
		}
		if (!t) {
			return;
		}
		while (task_list && !task_list->type) {
			pop_head_free();
		}
		t = task_list;
		prev = gGameUpdateTime;
	}
	gGameUpdateTime = prev;
}

void Sched_RenderAll(uint32_t time) { // 0x4427d0
	for (task_t *t = task_list; t; t = t->next) {
		if (t->type && t != gCameraTask && t->type->render) {
			t->type->render(t, time);
		}
	}
}

void Sched_RenderReflective(uint32_t time) { // 0x442810
	for (task_t *t = task_list; t; t = t->next) {
		if (t->type && (t->type->flags & 8) && t->type->render) {
			t->type->render(t, time);
		}
	}
}

void Sched_RenderCamera(uint32_t time) { // 0x442840
	if (gCameraTask && gCameraTask->type && gCameraTask->type->render) {
		gCameraTask->type->render(gCameraTask, time);
	}
}

void Sched_DestroyAll(void) { // 0x442870
	while (task_list) {
		if (task_list->type) {
			Task_Kill(task_list);
		}
		pop_head_free();
	}
}
