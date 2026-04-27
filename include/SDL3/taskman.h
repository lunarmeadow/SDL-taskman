#ifndef _TASKMAN_H_
#define _TASKMAN_H_

#define TASK_TIMER_HZ 0x10000
#define SYS_TIMER_HZ 18.2f
#define PIT_CLOCK 1193182
#define MAX_UINT32 4294967295

#define MAX_TASKS 2

// thank you doom8088 very cool
#define _call_systimer(old_handler) \
	asm \
	( \
		"cli \n" \
		"pushfl \n" \
		"lcall *%0" \
		: \
		: "m" (old_handler.pm_offset) \
	)

#define _disable_irq() \
	asm \
	( \
		"cli\n" \
	)

#define _enable_irq() \
	asm \
	( \
		"sti\n" \
	)

typedef struct task {
	// call this function when interval > elapsed_tics
	void (*handler)(struct task* this);

	// how many IRQs has this task run for,
	// should we proceess?
	volatile unsigned long elapsed_tics;

	// how many tics (62.5us) for periodic execution
	// divide the task timer hz by desired task hz for this
	// e.g. to run an 18.2hz task, 16016 / 18.2 = 880
	volatile unsigned long interval;

	// wait for task to be finished before changing state
	volatile unsigned int isProcessing;
	volatile unsigned int isActive;
	volatile unsigned int isInit;

	// how many tics to max out on pause?
	volatile unsigned int maxPauseTics;
} task_t;

extern void TaskMan_DisableTasks(void);
extern void TaskMan_EnableTasks(void);
extern void TaskMan_AddTask(void (*func)(struct task* this), unsigned long hz, int maxPauseTics);
extern void TaskMan_Init(void);
extern void TaskMan_Shutdown(void);
extern unsigned long TaskMan_GetTics(void);

#endif // _TASKMAN_H_
