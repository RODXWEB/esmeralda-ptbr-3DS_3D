#include "global.h"
#include "task.h"

#ifdef PORT_BRIDGE
#include "port_platform.h"
#endif

COMMON_DATA struct Task gTasks[NUM_TASKS] = {0};

static void InsertTask(u8 newTaskId);
static u8 FindFirstActiveTask(void);

#ifdef PORT_BRIDGE
static bool8 TaskIsActiveRaw(u8 taskId)
{
    const volatile u8 *taskRaw = (const volatile u8 *)&gTasks[taskId];

    return taskRaw[4] == TRUE;
}

static u8 TaskPrevRaw(u8 taskId)
{
    const volatile u8 *taskRaw = (const volatile u8 *)&gTasks[taskId];

    return taskRaw[5];
}

static u8 TaskNextRaw(u8 taskId)
{
    const volatile u8 *taskRaw = (const volatile u8 *)&gTasks[taskId];

    return taskRaw[6];
}

static u8 TaskPriorityRaw(u8 taskId)
{
    const volatile u8 *taskRaw = (const volatile u8 *)&gTasks[taskId];

    return taskRaw[7];
}
#endif

void ResetTasks(void)
{
    u8 i;

    for (i = 0; i < NUM_TASKS; i++)
    {
        gTasks[i].isActive = FALSE;
        gTasks[i].func = TaskDummy;
        gTasks[i].prev = i;
        gTasks[i].next = i + 1;
        gTasks[i].priority = -1;
        memset(gTasks[i].data, 0, sizeof(gTasks[i].data));
    }

    gTasks[0].prev = HEAD_SENTINEL;
    gTasks[NUM_TASKS - 1].next = TAIL_SENTINEL;
}

u8 CreateTask(TaskFunc func, u8 priority)
{
    u8 i;

    for (i = 0; i < NUM_TASKS; i++)
    {
        #ifdef PORT_BRIDGE
        if (!TaskIsActiveRaw(i))
        #else
        if (!gTasks[i].isActive)
        #endif
        {
            gTasks[i].func = func;
            gTasks[i].priority = priority;
            InsertTask(i);
            memset(gTasks[i].data, 0, sizeof(gTasks[i].data));
            gTasks[i].isActive = TRUE;
            return i;
        }
    }

    return 0;
}

static void InsertTask(u8 newTaskId)
{
    u8 taskId = FindFirstActiveTask();

    if (taskId == NUM_TASKS)
    {
        // The new task is the only task.
        gTasks[newTaskId].prev = HEAD_SENTINEL;
        gTasks[newTaskId].next = TAIL_SENTINEL;
        return;
    }

    while (1)
    {
        #ifdef PORT_BRIDGE
        if (gTasks[newTaskId].priority < TaskPriorityRaw(taskId))
        #else
        if (gTasks[newTaskId].priority < gTasks[taskId].priority)
        #endif
        {
            // We've found a task with a higher priority value,
            // so we insert the new task before it.
            #ifdef PORT_BRIDGE
            gTasks[newTaskId].prev = TaskPrevRaw(taskId);
            #else
            gTasks[newTaskId].prev = gTasks[taskId].prev;
            #endif
            gTasks[newTaskId].next = taskId;
            #ifdef PORT_BRIDGE
            if (TaskPrevRaw(taskId) != HEAD_SENTINEL)
                gTasks[TaskPrevRaw(taskId)].next = newTaskId;
            #else
            if (gTasks[taskId].prev != HEAD_SENTINEL)
                gTasks[gTasks[taskId].prev].next = newTaskId;
            #endif
            gTasks[taskId].prev = newTaskId;
            return;
        }
        #ifdef PORT_BRIDGE
        if (TaskNextRaw(taskId) == TAIL_SENTINEL)
        #else
        if (gTasks[taskId].next == TAIL_SENTINEL)
        #endif
        {
            // We've reached the end.
            gTasks[newTaskId].prev = taskId;
            #ifdef PORT_BRIDGE
            gTasks[newTaskId].next = TaskNextRaw(taskId);
            #else
            gTasks[newTaskId].next = gTasks[taskId].next;
            #endif
            gTasks[taskId].next = newTaskId;
            return;
        }
        #ifdef PORT_BRIDGE
        taskId = TaskNextRaw(taskId);
        #else
        taskId = gTasks[taskId].next;
        #endif
    }
}

void DestroyTask(u8 taskId)
{
    #ifdef PORT_BRIDGE
    if (TaskIsActiveRaw(taskId))
    #else
    if (gTasks[taskId].isActive)
    #endif
    {
        gTasks[taskId].isActive = FALSE;

        #ifdef PORT_BRIDGE
        if (TaskPrevRaw(taskId) == HEAD_SENTINEL)
        {
            if (TaskNextRaw(taskId) != TAIL_SENTINEL)
                gTasks[TaskNextRaw(taskId)].prev = HEAD_SENTINEL;
        }
        else
        {
            if (TaskNextRaw(taskId) == TAIL_SENTINEL)
            {
                gTasks[TaskPrevRaw(taskId)].next = TAIL_SENTINEL;
            }
            else
            {
                gTasks[TaskPrevRaw(taskId)].next = TaskNextRaw(taskId);
                gTasks[TaskNextRaw(taskId)].prev = TaskPrevRaw(taskId);
            }
        }
        #else
        if (gTasks[taskId].prev == HEAD_SENTINEL)
        {
            if (gTasks[taskId].next != TAIL_SENTINEL)
                gTasks[gTasks[taskId].next].prev = HEAD_SENTINEL;
        }
        else
        {
            if (gTasks[taskId].next == TAIL_SENTINEL)
            {
                gTasks[gTasks[taskId].prev].next = TAIL_SENTINEL;
            }
            else
            {
                gTasks[gTasks[taskId].prev].next = gTasks[taskId].next;
                gTasks[gTasks[taskId].next].prev = gTasks[taskId].prev;
            }
        }
        #endif
    }
}

void RunTasks(void)
{
#ifdef PORT_BRIDGE
    u8 taskId = FindFirstActiveTask();

    Port_SetDmaDebugEx(0x0026, (u16)((uintptr_t)gTasks >> 16), (u16)(uintptr_t)gTasks, gTasks[0].isActive | (gTasks[0].prev << 8), gTasks[0].next | (gTasks[0].priority << 8));

    if (taskId != NUM_TASKS)
    {
        do
        {
            uintptr_t func = (uintptr_t)gTasks[taskId].func;
            Port_SetDmaDebugEx(0x0024, taskId, TaskIsActiveRaw(taskId) | (TaskPrevRaw(taskId) << 8), (u16)(func >> 16), (u16)func);
            gTasks[taskId].func(taskId);
            func = (uintptr_t)gTasks[taskId].func;
            Port_SetDmaDebugEx(0x0025, taskId, TaskIsActiveRaw(taskId) | (TaskNextRaw(taskId) << 8), (u16)(func >> 16), (u16)func);
            taskId = TaskNextRaw(taskId);
        } while (taskId != TAIL_SENTINEL);
    }
#else
    u8 taskId = FindFirstActiveTask();

    if (taskId != NUM_TASKS)
    {
        do
        {
            gTasks[taskId].func(taskId);
            taskId = gTasks[taskId].next;
        } while (taskId != TAIL_SENTINEL);
    }
#endif
}

static u8 FindFirstActiveTask(void)
{
    u8 taskId;
#ifdef PORT_BRIDGE
    const u8 *raw = (const u8 *)gTasks;
#endif

#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0027, (u16)((uintptr_t)gTasks >> 16), (u16)(uintptr_t)gTasks, gTasks[0].isActive | (gTasks[0].prev << 8), gTasks[0].next | (gTasks[0].priority << 8));
#endif

    for (taskId = 0; taskId < NUM_TASKS; taskId++)
    {
#ifdef PORT_BRIDGE
        if (TaskIsActiveRaw(taskId) && TaskPrevRaw(taskId) == HEAD_SENTINEL)
            break;
#else
        if (gTasks[taskId].isActive == TRUE && gTasks[taskId].prev == HEAD_SENTINEL)
            break;
#endif
    }

#ifdef PORT_BRIDGE
    if (taskId < NUM_TASKS)
        Port_SetDmaDebugEx(0x0023, taskId, gTasks[taskId].isActive | (gTasks[taskId].prev << 8), gTasks[taskId].next, 0);
    else
        Port_SetDmaDebugEx(0x0023, taskId, 0, 0, 0);

    Port_SetDmaDebug2(0x0030,
                     raw[4] | (raw[5] << 8),
                     raw[6] | (raw[7] << 8),
                     raw[40] | (raw[41] << 8),
                     raw[42] | (raw[43] << 8));
#endif

    return taskId;
}

void TaskDummy(u8 taskId)
{
}

void SetTaskFuncWithFollowupFunc(u8 taskId, TaskFunc func, TaskFunc followupFunc)
{
    u8 followupFuncIndex = NUM_TASK_DATA - 2; // Should be const.

    gTasks[taskId].data[followupFuncIndex] = (s16)((u32)followupFunc);
    gTasks[taskId].data[followupFuncIndex + 1] = (s16)((u32)followupFunc >> 16); // Store followupFunc as two half-words in the data array.
    gTasks[taskId].func = func;
}

void SwitchTaskToFollowupFunc(u8 taskId)
{
    u8 followupFuncIndex = NUM_TASK_DATA - 2; // Should be const.

    gTasks[taskId].func = (TaskFunc)((u16)(gTasks[taskId].data[followupFuncIndex]) | (gTasks[taskId].data[followupFuncIndex + 1] << 16));
}

bool8 FuncIsActiveTask(TaskFunc func)
{
    u8 i;

    for (i = 0; i < NUM_TASKS; i++)
        #ifdef PORT_BRIDGE
        if (TaskIsActiveRaw(i) && gTasks[i].func == func)
        #else
        if (gTasks[i].isActive == TRUE && gTasks[i].func == func)
        #endif
            return TRUE;

    return FALSE;
}

u8 FindTaskIdByFunc(TaskFunc func)
{
    s32 i;

    for (i = 0; i < NUM_TASKS; i++)
        #ifdef PORT_BRIDGE
        if (TaskIsActiveRaw(i) && gTasks[i].func == func)
        #else
        if (gTasks[i].isActive == TRUE && gTasks[i].func == func)
        #endif
            return (u8)i;

    return TASK_NONE; // No task was found.
}

u8 GetTaskCount(void)
{
    u8 i;
    u8 count = 0;

    for (i = 0; i < NUM_TASKS; i++)
        #ifdef PORT_BRIDGE
        if (TaskIsActiveRaw(i))
        #else
        if (gTasks[i].isActive == TRUE)
        #endif
            count++;

    return count;
}

void SetWordTaskArg(u8 taskId, u8 dataElem, u32 value)
{
    if (dataElem < NUM_TASK_DATA - 1)
    {
        gTasks[taskId].data[dataElem] = value;
        gTasks[taskId].data[dataElem + 1] = value >> 16;
    }
}

u32 GetWordTaskArg(u8 taskId, u8 dataElem)
{
    if (dataElem < NUM_TASK_DATA - 1)
        return (u16)gTasks[taskId].data[dataElem] | (gTasks[taskId].data[dataElem + 1] << 16);
    else
        return 0;
}
