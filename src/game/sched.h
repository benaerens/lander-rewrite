// Game clock and the time-ordered task scheduler (port of 0x442000-0x442900).
// Every game process (objects, physics, input polling, timers, events, delayed
// calls) is a task with a wake time; Sched_RunUntil runs the due tasks in wake
// order (FIFO among equal times) and re-schedules each after its type's period.
#ifndef SCHED_H
#define SCHED_H

#include "../types.h"

typedef struct task_t task_t;

typedef struct task_type_t {
	uint32_t flags;                                  // bit0: has render, bit3: reflection pass, class << 16
	void (*update)(task_t *t);
	void (*render)(task_t *t, uint32_t time);
	void (*init)(task_t *t, void *arg);
	void (*destroy)(task_t *t);
	void (*save)(task_t *t, uint8_t **cursor);
	void (*load)(uint8_t **cursor);
	uint32_t period;                                 // ms between updates
} task_type_t;

struct task_t {
	task_t *next;
	uint32_t wake;                                   // game ms
	const task_type_t *type;                         // NULL = dead, freed when it reaches the head
	intptr_t data[8];                                // type-specific state (0x0c.. in the original)
};

#define MAX_TASKS 512

extern int32_t gGameUpdateTime;                     // time of the task being updated (compared unsigned)
extern int32_t gLevelEndFlag;
extern bool gSchedStall;
extern task_t *gCameraTask;

// clock (game milliseconds)
void Clock_Reset(void);
void Clock_Set(uint32_t ms);
uint32_t Clock_Update(void);                         // advances by real time (gaps > 5 s skipped)
uint32_t Clock_Now(void);
extern uint32_t gTimeScale;                          // 4.12, 0x1000 = real time

void Sched_Reset(void);
task_t *Task_Create(const task_type_t *type, void *arg, int32_t delay);
task_t *Task_CreateNow(const task_type_t *type, void *arg);
void Task_Kill(task_t *t);
void Task_SetWakeTime(task_t *t, uint32_t wake);
int Task_ToIndex(const task_t *t);
task_t *Task_FromIndex(int index);
void Sched_RunUntil(uint32_t time);
void Sched_RenderAll(uint32_t time);
void Sched_RenderReflective(uint32_t time);
void Sched_RenderCamera(uint32_t time);
void Sched_DestroyAll(void);
int Sched_FreeCount(void);
task_t *Sched_GetHead(void);

#endif
