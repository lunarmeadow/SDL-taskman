#include <SDL3/taskman.h>

#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <dos.h>
#include <conio.h>
#include <dpmi.h>

#if defined(SDL_PLATFORM_DOS)

static int taskman_started = 0;

// how many irqs have run

// interval/tracker for system timer
volatile static unsigned long serviceRate = 0;
volatile static unsigned long serviceCount = 0;
volatile static unsigned long irqCount = 0;

static task_t tasks[MAX_TASKS];

// index of last added task
volatile static int task_cursor = 0;

// maintain old 18.2hz system clock sync, call it within our new function and reset to it
static _go32_dpmi_seginfo old_task_handler, task_handler;

// add task via callback ptr and hertz, 
// and specify whether it should accumulate time when paused 
// (-1 for max tics, 0 for no tics, N for N tics)
void TaskMan_AddTask(void (*func)(struct task* this), unsigned long hz, int maxPauseTics)
{
	int calc_interval = (int)((float)PIT_CLOCK / hz);

	task_t task = {
		.interval = calc_interval,
		.handler = func,
		.isActive = 1,
		.isProcessing = 0,
		.isInit = 1,
		.maxPauseTics = maxPauseTics
	};

	if(task_cursor < 0)
		task_cursor = 0;

	_disable_irq();

	if(task_cursor <= MAX_TASKS - 1)
		tasks[task_cursor++] = task;

	_enable_irq();
}

void __attribute__((hot)) task_irq(void)
{
	_disable_irq();

	++irqCount;

	for(int t = 0; t < MAX_TASKS; t++)
	{
		if(!tasks[t].isInit)
			continue;

		if(tasks[t].isActive)
		{
			// lock the task
			tasks[t].isProcessing = 1;

			tasks[t].elapsed_tics += serviceRate;

			// clear off all scheduled tasks,
			// remainder here will carry over
			while(tasks[t].elapsed_tics >= tasks[t].interval)
			{
				tasks[t].elapsed_tics -= tasks[t].interval;
				tasks[t].handler(&tasks[t]);
			}

			// unlock the task
			tasks[t].isProcessing = 0;
		}

		else if(tasks[t].maxPauseTics)
		{
			// hard cap accumulated processing tics while paused to integer limit
			// overflow would cause serious issues for long-stalled tasks
			if(tasks[t].elapsed_tics + serviceRate > MAX_UINT32)
				continue;

			// for any negative N, there is no ceiling but the logical hard-limit
			// for any positive N, the ceiling is ticRate * N
			if(tasks[t].maxPauseTics < 0 ||
				(tasks[t].elapsed_tics + serviceRate) < 
				(serviceRate * tasks[t].maxPauseTics))
			{
				tasks[t].elapsed_tics += serviceRate;
			}
		}
	}

	_enable_irq();
	
	// done :3
	// this calls original dos clock interrupt approximately at original rate
	serviceCount += serviceRate;
	if(serviceCount > 0xFFFFl)
	{
		serviceCount &= 0xFFFF;
		_call_systimer(old_task_handler);
	}
	else
	{
		outp(0x20, 0x20);
	}
}

void __attribute__((hot)) TaskMan_DisableTasks(void)
{
	for(int t = 0; t < MAX_TASKS; t++)
	{
		if(tasks[t].isActive && tasks[t].isInit)
		{
			tasks[t].isActive = 0;
		}
	}
}

void __attribute__((hot)) TaskMan_EnableTasks(void)
{
	for(int t = 0; t < MAX_TASKS; t++)
	{
		if(!tasks[t].isActive && tasks[t].isInit)
		{
			tasks[t].isActive = 1;
		}
	}
}

void task_irq_end(void) {}

void TaskMan_SetClock(unsigned long rate)
{
	_disable_irq();

	// send command to PIT to listen for frequency timing bits
	outp(0x43, 0x36);

	// write lo and hi 16 bits of target clock to data port of PIT channel zero
	outp(0x40, (unsigned char)(serviceRate & 0xFF));
	outp(0x40, (unsigned char)((serviceRate >> 8) & 0xFF));

	_enable_irq();
}

void TaskMan_Init(void)
{
	if(!taskman_started)
	{
		serviceRate = PIT_CLOCK / TASK_TIMER_HZ;

		memset((void *)tasks, 0, sizeof(tasks));
		_go32_dpmi_lock_data((void *)tasks, sizeof(tasks));
		_go32_dpmi_lock_code(task_irq, (unsigned long)task_irq_end - (unsigned long)task_irq);

		_go32_dpmi_get_protected_mode_interrupt_vector(8, &old_task_handler);

		task_handler.pm_offset = (int)task_irq;

		_go32_dpmi_allocate_iret_wrapper(&task_handler);
		_go32_dpmi_set_protected_mode_interrupt_vector(8, &task_handler);

		TaskMan_SetClock(serviceRate);

		taskman_started = 1;
	}
}

void TaskMan_Shutdown(void)
{
	if(taskman_started)
	{
		// send command to PIT to listen for frequency timing bits
		outp(0x43, 0x34);

		// clear lo and hi divider bits
		outp(0x40, 0x00);
		outp(0x40, 0x00);

		_go32_dpmi_set_protected_mode_interrupt_vector(8, &old_task_handler);
		_go32_dpmi_free_iret_wrapper(&task_handler);

		taskman_started = 0;
	}
}

// 125us interval
unsigned long __attribute__((hot)) TaskMan_GetTics()
{
	return irqCount;
}

#endif