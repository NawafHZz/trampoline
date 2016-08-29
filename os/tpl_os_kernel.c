/**
 * @file tpl_os_kernel.c
 *
 * @section descr File description
 *
 * Trampoline kernel structures and functions. These functions are used
 * internally by trampoline and should not be used directly.
 *
 * @section copyright Copyright
 *
 * Trampoline RTOS
 *
 * Trampoline is copyright (c) CNRS, University of Nantes, Ecole Centrale de Nantes
 * Trampoline is protected by the French intellectual property law.
 *
 * This software is distributed under the GNU Public Licence V2.
 * Check the LICENSE file in the root directory of Trampoline
 *
 * @section infos File informations
 *
 * $Date$
 * $Rev$
 * $Author$
 * $URL$
 *
 */

#include "tpl_os_kernel.h"
#include "tpl_os_definitions.h"
#include "tpl_os_hooks.h"
#include "tpl_os_error.h"
#include "tpl_os_alarm_kernel.h"
#include "tpl_machine.h"
#include "tpl_machine_interface.h"
#include "tpl_dow.h"
#include "tpl_trace.h"
#include "tpl_os_interrupt.h"
#include "tpl_os_interrupt_kernel.h"
#include "tpl_os_resource_kernel.h"
#include "tpl_os_task.h"

#if WITH_STACK_MONITORING == YES
#include "tpl_as_stack_monitor.h"
#endif
#if WITH_AUTOSAR == YES
#include "tpl_as_st_kernel.h"
#if AUTOSAR_SC == 3 || AUTOSAR_SC == 4
#include "tpl_as_app_kernel.h"
#endif
#endif
#if WITH_AUTOSAR_TIMING_PROTECTION == YES
#include "tpl_as_protec_hook.h"
#endif
#if ((WITH_IOC == YES) && (IOC_UNQUEUED_COUNT > 0))
# include "tpl_ioc_unqueued_kernel.h"
#endif

#define OS_START_SEC_CONST_UNSPECIFIED
#include "tpl_memmap.h"

/**
 * @def INVALID_PROC
 *
 * This value is used to specify an invalid process (Task or ISR category 2)
 */
CONST(tpl_proc_id, AUTOMATIC) INVALID_PROC = INVALID_PROC_ID;

/**
 * @def INVALID_TASK
 *
 * This value is used to specify an invalid #TaskType
 */
CONST(tpl_proc_id, AUTOMATIC) INVALID_TASK = INVALID_PROC_ID;


#define OS_STOP_SEC_CONST_UNSPECIFIED
#include "tpl_memmap.h"

#define OS_START_SEC_VAR_UNSPECIFIED
#include "tpl_memmap.h"

/*
 * MISRA RULE 27 VIOLATION: These 4 variables are used only in this file
 * but declared in the configuration file, this is why they do not need
 * to be declared as external in a header file
 */

#if TASK_COUNT > 0
/**
 * @internal
 *
 * tpl_task_app_mode is a table that is automatically generated by goil from
 * the application description. Indexes of this table are the tasks'
 * identifiers.
 * each of its elements is an application mode mask. For an AUTOSTART task with
 * identifier id in application mode app_mode, tpl_task_app_mode[id] & app_mode
 * will be true.
 */
extern CONST(tpl_appmode_mask, OS_CONST) tpl_task_app_mode[TASK_COUNT];

#endif

#if ALARM_COUNT > 0

/**
 * @internal
 *
 * tpl_alarm_app_mode is a table that is automatically generated by goil from
 * the application description. Indexes of this table are the alarms'
 * identifiers.
 * each of its elements is an application mode mask.
 * For an AUTOSTART alarm with identifier id in application mode app_mode,
 * tpl_alarm_app_mode[id] & app_mode will be true.
 */
extern CONST(tpl_appmode_mask, OS_CONST) tpl_alarm_app_mode[ALARM_COUNT];

#endif

#if SCHEDTABLE_COUNT > 0

/**
 * @internal
 *
 * tpl_scheduletable_app_mode is a table that is automatically generated by
 * goil from the application description. Indexes of this table are the
 * schedule tables' identifiers. each of its elements is an application mode
 * mask. For an AUTOSTART schedule table with identifier id in application
 * mode app_mode, tpl_scheduletable_app_mode[id] & app_mode will be true.
 */
extern CONST(tpl_appmode_mask, OS_CONST)
  tpl_scheduletable_app_mode[SCHEDTABLE_COUNT];

#endif

/**
 * INTERNAL_RES_SCHEDULER is an internal resource with the higher task
 * priority in the application. A task is non preemptable when
 * INTERNAL_RES_SCHEDULER is set as internal resource.
 */
VAR(tpl_internal_resource, OS_VAR) INTERNAL_RES_SCHEDULER = {
    RES_SCHEDULER_PRIORITY, /**< the ceiling priority is defined as the
                                 maximum priority of the tasks of the
                                 application */
    0,
    FALSE
};

#define OS_STOP_SEC_VAR_UNSPECIFIED
#include "tpl_memmap.h"


#define OS_START_SEC_CODE
#include "tpl_memmap.h"

#ifdef WITH_DOW
#include <stdio.h>

extern CONSTP2CONST(char, AUTOMATIC, OS_APPL_DATA) proc_name_table[];

/*
 * MISRA RULE 13 VIOLATION: this function is only used for debug purpose,
 * so production release is not impacted by MISRA rules violated
 * in this function
 */
FUNC(void, OS_CODE) printrl(P2VAR(char, AUTOMATIC, OS_APPL_DATA) msg)
{
#if NUMBER_OF_CORES > 1
  /* TODO */
#else
  uint32 i;
  printf("ready list %s [%d]", msg, tpl_ready_list[0].key);
  for (i = 1; i <= tpl_ready_list[0].key; i++)
  {
    printf(" {%d/%d,%s[%d](%d)}",
           (int)(tpl_ready_list[i].key >> PRIORITY_SHIFT),
           (int)(tpl_ready_list[i].key & RANK_MASK),
           proc_name_table[tpl_ready_list[i].id],
           (int)tpl_ready_list[i].id,
           tpl_ready_list[i].key);
  }
  printf("\n");
#endif
}

FUNC(void, OS_CODE) print_kern(P2VAR(char, AUTOMATIC, OS_APPL_DATA) msg)
{
#if NUMBER_OF_CORES > 1
  /* TODO */
#else
  printf("KERN %s running : %s[%ld](%d), elected : %s[%ld](%d)\n",
    msg,
    (tpl_kern.running_id == INVALID_TASK) ? "NONE" : proc_name_table[tpl_kern.running_id],
    tpl_kern.running_id,
    (tpl_kern.running_id == INVALID_TASK) ? -1 : tpl_kern.running->priority,
    (tpl_kern.elected_id == INVALID_TASK) ? "NONE" : proc_name_table[tpl_kern.elected_id],
    tpl_kern.elected_id,
    (tpl_kern.elected_id == INVALID_TASK) ? -1 : tpl_kern.elected->priority
  );
#endif
}

#endif

/*
 * Jobs are stored in a heap. Each entry has a key (used to sort the heap)
 * and the id of the process. The size of the heap is computed by doing
 * the sum of the activations of a process (each activation of a process is
 * a job. The key is the concatenation of the priority of the job and the
 * rank of the job. The max value is a higher rank.
 *
 * The head of the queue contains the highest priority job and so the running
 * job.
 *
 * RANK_MASK is used to get the part of the key used to store the rank of
 * a job
 * PRIORITY_MASK is used to get the part of the key used to store
 * the priority of a job
 * PRIORITY_SHIFT is used to shift the part of the key used to store
 * the priority of a job
 */
FUNC(int, OS_CODE) tpl_compare_entries(
  CONSTP2CONST(tpl_heap_entry, AUTOMATIC, OS_VAR) first_entry,
  CONSTP2CONST(tpl_heap_entry, AUTOMATIC, OS_VAR) second_entry
  TAIL_FOR_PRIO_ARG_DECL(tail_for_prio))
{
  VAR(uint32, AUTOMATIC) first_key = first_entry->key & (PRIORITY_MASK | RANK_MASK);
  VAR(uint32, AUTOMATIC) second_key = second_entry->key & (PRIORITY_MASK | RANK_MASK);
  VAR(uint32, AUTOMATIC) first_tmp ;
  VAR(uint32, AUTOMATIC) second_tmp ;

  first_tmp = ((first_key & RANK_MASK) -
    TAIL_FOR_PRIO(tail_for_prio)[first_key >> PRIORITY_SHIFT]);
  first_tmp = first_tmp & RANK_MASK;
  first_key = (first_key & PRIORITY_MASK);
  first_key = first_key | first_tmp;

  second_tmp = ((second_key & RANK_MASK) -
    TAIL_FOR_PRIO(tail_for_prio)[second_key >> PRIORITY_SHIFT]);
  second_tmp = second_tmp & RANK_MASK;
  second_key = (second_key & PRIORITY_MASK);
  second_key = second_key | second_tmp;

  return (first_key < second_key);
}

/*
 * @internal
 *
 * tpl_bubble_up bubbles the entry at index place up in the heap
 *
 * @param  heap   the heap on which the operation is done
 * @param  index  the place of the item to bubble up
 *
 */
FUNC(void, OS_CODE) tpl_bubble_up(
  CONSTP2VAR(tpl_heap_entry, AUTOMATIC, OS_VAR) heap,
  VAR(uint32, AUTOMATIC)                        index
  TAIL_FOR_PRIO_ARG_DECL(tail_for_prio))
{
  VAR(uint32, AUTOMATIC) father = index >> 1;

  while ((index > 1) &&
    (tpl_compare_entries(heap + father,
                         heap + index
                         TAIL_FOR_PRIO_ARG(tail_for_prio))))
  {
    /*
     * if the father key is lower then the index key, swap them
     */
    VAR(tpl_heap_entry, AUTOMATIC) tmp = heap[index];
    heap[index] = heap[father];
    heap[father] = tmp;
    index = father;
    father >>= 1;
  }
}

/*
 * @internal
 *
 * tpl_bubble_down bubbles the entry at index place down in the heap
 *
 * @param  heap   the heap on which the operation is done
 * @param  index  the place of the item to bubble down
 *
 */
FUNC(void, OS_CODE) tpl_bubble_down(
  CONSTP2VAR(tpl_heap_entry, AUTOMATIC, OS_VAR) heap,
  VAR(uint32, AUTOMATIC)                        index
  TAIL_FOR_PRIO_ARG_DECL(tail_for_prio))
{
  CONST(uint32, AUTOMATIC) size = heap[0].key;
  VAR(uint32, AUTOMATIC) child;

  while ((child = index << 1) <= size) /* child = left */
  {
    CONST(uint32, AUTOMATIC) right = child + 1;
    if ((right <= size) &&
      tpl_compare_entries(heap + child,
                          heap + right
                          TAIL_FOR_PRIO_ARG(tail_for_prio)))
    {
      /* the right child exists and is greater */
      child = right;
    }
    if (tpl_compare_entries(heap + index,
                            heap + child
                            TAIL_FOR_PRIO_ARG(tail_for_prio)))
    {
      /* the father has a key <, swap */
      CONST(tpl_heap_entry, AUTOMATIC) tmp = heap[index];
      heap[index] = heap[child];
      heap[child] = tmp;
      /* go down */
      index = child;
    }
    else
    {
      /* went down to its place, stop the loop */
      break;
    }
  }
}

/*
 * @internal
 *
 * tpl_put_new_proc puts a new proc in a ready list. In a multicore kernel
 * it may be called from a core that not own the ready list (for
 * a partitioned scheduler). So the core_id field of the proc descriptor
 * is used to get the corresponding ready list.
 */
FUNC(void, OS_CODE) tpl_put_new_proc(
  CONST(tpl_proc_id, AUTOMATIC) proc_id)
{
  GET_PROC_CORE_ID(proc_id, core_id)
  GET_CORE_READY_LIST(core_id, ready_list)
  GET_TAIL_FOR_PRIO(core_id, tail_for_prio)

  VAR(uint32, AUTOMATIC) index = (uint32)(++(READY_LIST(ready_list)[0].key));

  VAR(tpl_priority, AUTOMATIC) dyn_prio;
  CONST(tpl_priority, AUTOMATIC) prio =
    tpl_stat_proc_table[proc_id]->base_priority;
  /*
   * add the new entry at the end of the ready list
   */
  dyn_prio = (prio << PRIORITY_SHIFT) |
             (--TAIL_FOR_PRIO(tail_for_prio)[prio] & RANK_MASK);

  DOW_DO(printf("put new %s, %d\n",proc_name_table[proc_id],dyn_prio);)

  READY_LIST(ready_list)[index].key = dyn_prio;
  READY_LIST(ready_list)[index].id = proc_id;

  tpl_bubble_up(
    READY_LIST(ready_list),
    index
    TAIL_FOR_PRIO_ARG(tail_for_prio)
  );

  DOW_DO(printrl("put_new_proc");)
}

/*
 * @internal
 *
 * tpl_put_preempted_proc puts a preempted proc in a ready list.
 * In a multicore kernel it may be called from a core that does not own
 * the ready list (for a partitioned scheduler). So the core_id field
 * of the proc descriptor is used to get the corresponding ready list.
 */
FUNC(void, OS_CODE) tpl_put_preempted_proc(
  CONST(tpl_proc_id, AUTOMATIC) proc_id)
{
  GET_PROC_CORE_ID(proc_id, core_id)
  GET_CORE_READY_LIST(core_id, ready_list)
  GET_TAIL_FOR_PRIO(core_id, tail_for_prio)


  VAR(uint32, AUTOMATIC) index = (uint32)(++(READY_LIST(ready_list)[0].key));

  CONST(tpl_priority, AUTOMATIC) dyn_prio =
    tpl_dyn_proc_table[proc_id]->priority;

  DOW_DO(printf("put preempted %s, %d\n",proc_name_table[proc_id],dyn_prio));
  /*
   * add the new entry at the end of the ready list
   */
  READY_LIST(ready_list)[index].key = dyn_prio;
  READY_LIST(ready_list)[index].id = proc_id;

  tpl_bubble_up(
    READY_LIST(ready_list),
    index
    TAIL_FOR_PRIO_ARG(tail_for_prio)
  );

  DOW_DO(printrl("put_preempted_proc"));
}

/**
 * @internal
 *
 */

/**
 * @internal
 *
 * tpl_front_proc returns the proc_id of the highest priority proc in the
 * ready list on the current core
 */
FUNC(tpl_heap_entry, OS_CODE) tpl_front_proc(CORE_ID_OR_VOID(core_id))
{
  GET_CORE_READY_LIST(core_id, ready_list)

  return (READY_LIST(ready_list)[1]);
}

/*
 * @internal
 *
 * tpl_remove_front_proc removes the highest priority proc from the
 * ready list on the specified core and returns the heap_entry
 */
FUNC(tpl_heap_entry, OS_CODE) tpl_remove_front_proc(CORE_ID_OR_VOID(core_id))
{
  GET_CORE_READY_LIST(core_id, ready_list)
  GET_TAIL_FOR_PRIO(core_id, tail_for_prio)

  /*
   * Get the current size and update it (since the front element will be
   * removed)
   */
  CONST(uint32, AUTOMATIC) size = READY_LIST(ready_list)[0].key--;
  VAR(uint32, AUTOMATIC) index = 1;

  /*
   * Get the proc_id of the front proc
   */
  VAR(tpl_heap_entry, AUTOMATIC) proc = READY_LIST(ready_list)[1];

  /*
   * Put the last element in front
   */
  READY_LIST(ready_list)[index] = READY_LIST(ready_list)[size];

  /*
   * bubble down to the right place
   */
  tpl_bubble_down(
    READY_LIST(ready_list),
    index
    TAIL_FOR_PRIO_ARG(tail_for_prio)
  );

  return proc;
}



#if WITH_OSAPPLICATION == YES

/**
 * @internal
 *
 * tpl_remove_proc removes all the process instances in the ready queue
 */
FUNC(void, OS_CODE) tpl_remove_proc(CONST(tpl_proc_id, AUTOMATIC) proc_id)
{
  GET_PROC_CORE_ID(proc_id, core_id)
  GET_CORE_READY_LIST(core_id, ready_list)
  GET_TAIL_FOR_PRIO(core_id, tail_for_prio)

  VAR(uint32, AUTOMATIC) index;
  VAR(uint32, AUTOMATIC) size = (uint32)READY_LIST(ready_list)[0].key;

  DOW_DO(printf("\n**** remove proc %d ****\n",proc_id);)
  DOW_DO(printrl("tpl_remove_proc - before");)


  for (index = 1; index <= size; index++)
  {
    if (READY_LIST(ready_list)[index].id == proc_id)
    {
      READY_LIST(ready_list)[index] = READY_LIST(ready_list)[size--];
      tpl_bubble_down(
        READY_LIST(ready_list),
        index
        TAIL_FOR_PRIO_ARG(tail_for_prio)
      );
    }
  }

  READY_LIST(ready_list)[0].key = size;

  DOW_DO(printrl("tpl_remove_proc - after");)
}

#endif /* WITH_OSAPPLICATION */

/**
 * @internal
 *
 * tpl_current_os_state returns the current state of the OS.
 *
 * @see #tpl_os_state
 */
FUNC(tpl_os_state, OS_CODE) tpl_current_os_state(
  CORE_ID_OR_VOID(core_id))
{
  VAR(tpl_os_state, OS_APPL_DATA) state = OS_UNKNOWN;

  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  if (TPL_KERN_REF(kern).running_id == INVALID_PROC_ID) {
    state = OS_INIT;
  }
  else if (TPL_KERN_REF(kern).running_id >= (TASK_COUNT + ISR_COUNT))
  {
    state = OS_IDLE;
  }
  else if (TPL_KERN_REF(kern).running_id < TASK_COUNT)
  {
    state = OS_TASK;
  }
  else if (TPL_KERN_REF(kern).running_id < (TASK_COUNT + ISR_COUNT) )
  {
    state = OS_ISR2;
  }

  return state;
}

/**
 * @internal
 *
 * Get an internal resource
 *
 * @param task task from which internal resource is got
 */
FUNC(void, OS_CODE) tpl_get_internal_resource(
  CONST(tpl_proc_id, AUTOMATIC) task_id)
{
  GET_PROC_CORE_ID(task_id, core_id)
  GET_TAIL_FOR_PRIO(core_id, tail_for_prio)

  CONSTP2VAR(tpl_internal_resource, AUTOMATIC, OS_APPL_DATA) rez =
    tpl_stat_proc_table[task_id]->internal_resource;

  if ((NULL != rez) && (FALSE == rez->taken))
  {
    rez->taken = TRUE;
    rez->owner_prev_priority = tpl_dyn_proc_table[task_id]->priority;
    tpl_dyn_proc_table[task_id]->priority =
      DYNAMIC_PRIO(rez->ceiling_priority, tail_for_prio);
  }
}

/**
 * @internal
 *
 * Release an internal resource
 *
 * @param task task from which internal resource is released
 */
FUNC(void, OS_CODE) tpl_release_internal_resource(
    CONST(tpl_proc_id, AUTOMATIC) task_id)
{
  CONSTP2VAR(tpl_internal_resource, AUTOMATIC, OS_APPL_DATA) rez =
    tpl_stat_proc_table[task_id]->internal_resource;

  if ((NULL != rez) && (TRUE == rez->taken))
  {
    rez->taken = FALSE;
    tpl_dyn_proc_table[task_id]->priority = rez->owner_prev_priority;
  }
}

/**
 * @internal
 *
 * Preempt the running process.
 */
FUNC(void, OS_CODE) tpl_preempt(CORE_ID_OR_VOID(core_id))
{
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  if (TPL_KERN_REF(kern).running_id != TPL_KERN_REF(kern).elected_id)
  {
    /*
     * since running and elected are different, the elected proc
     * is preempted but has not yet run. so it is put
     * back in the ready list. This occurs only in multicore
     * It is not a preemption actually so the PostTaskHook is not
     * called, the trace is not done and the timing protection
     * is not notified.
     */
    DOW_DO(print_kern("inside tpl_preempt"));

    /* The current elected task becomes READY */
    TPL_KERN_REF(kern).elected->state = (tpl_proc_state)READY;

    /* And put in the ready list */
    tpl_put_preempted_proc((tpl_proc_id)TPL_KERN_REF(kern).elected_id);
  }
}

/**
 * @internal
 *
 * The elected task becomes the running task, the running proc is preempted
 *
 * @return  the pointer to the context of the task
 *          that was running before the elected task replace it
 */
FUNC(P2CONST(tpl_context, AUTOMATIC, OS_CONST), OS_CODE)
  tpl_run_elected(CONST(tpl_bool, AUTOMATIC) save)
{
  GET_CURRENT_CORE_ID(core_id)
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  CONSTP2CONST(tpl_context, AUTOMATIC, OS_CONST) old_context =
    save ? &(TPL_KERN_REF(kern).s_running->context) : NULL;

  DOW_DO(print_kern("before tpl_run_elected"));

  if ((save) && (TPL_KERN_REF(kern).running->state != WAITING))
  {
    /*
     * The running task is preempted, so it is time to call the
     * PostTaskHook while the soon descheduled task is running
     */
    CALL_POST_TASK_HOOK()

    TRACE_ISR_PREEMPT((tpl_proc_id)TPL_KERN_REF(kern).running_id)
    TRACE_TASK_PREEMPT((tpl_proc_id)TPL_KERN_REF(kern).running_id)

    DOW_DO(printf(
      "tpl_run_elected preempt %s\n",
      proc_name_table[TPL_KERN_REF(kern).running_id])
    );

    /* The current running task becomes READY */
    TPL_KERN_REF(kern).running->state = (tpl_proc_state)READY;

    /* And put in the ready list */
    tpl_put_preempted_proc((tpl_proc_id)TPL_KERN_REF(kern).running_id);

  #if WITH_AUTOSAR_TIMING_PROTECTION == YES
    /* cancel the watchdog and update the budget                  */
    tpl_tp_on_preempt(TPL_KERN_REF(kern).running_id);
  #endif /* WITH_AUTOSAR_TIMING_PROTECTION */
  }
  /* copy the elected proc in running slot of tpl_kern */
  TPL_KERN_REF(kern).running = TPL_KERN_REF(kern).elected;
  TPL_KERN_REF(kern).s_running = TPL_KERN_REF(kern).s_elected;
  TPL_KERN_REF(kern).running_id = TPL_KERN_REF(kern).elected_id;

  DOW_DO(printf(
    "start %s, %d\n",
    proc_name_table[TPL_KERN_REF(kern).running_id],
    TPL_KERN_REF(kern).running->priority)
  );
  DOW_DO(printrl("tpl_run_elected - after"));

  /* the elected task become RUNNING */
  TRACE_TASK_EXECUTE((tpl_proc_id)TPL_KERN_REF(kern).running_id)
  TRACE_ISR_RUN((tpl_proc_id)TPL_KERN_REF(kern).running_id)
  TPL_KERN_REF(kern).running->state = RUNNING;

#if WITH_AUTOSAR_TIMING_PROTECTION == YES
  /*  start the budget watchdog  */
  tpl_tp_on_start(TPL_KERN_REF(kern).running_id);
#endif /* WITH_AUTOSAR_TIMING_PROTECTION */

  /*
   * If an internal resource is assigned to the task
   * and it is not already taken by it, take it
   */
  tpl_get_internal_resource((tpl_proc_id)TPL_KERN_REF(kern).running_id);

  /*
   * A new task has been started. It is time to call
   * PreTaskHook since the rescheduled task is running
   */
  CALL_PRE_TASK_HOOK()

  DOW_DO(print_kern("after tpl_run_elected"));

  return old_context;
}

/**
 * @internal
 *
 * Start the highest priority READY process
 */
FUNC(void, OS_CODE) tpl_start(CORE_ID_OR_VOID(core_id))
{
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  CONST(tpl_heap_entry, AUTOMATIC) proc =
    tpl_remove_front_proc(CORE_ID_OR_NOTHING(core_id));

#if NUMBER_OF_CORES > 1
  /*
   * In multicore, start may be called more than one time.
   * In this case a proc that is elected but not yet running
   * should be put back in the ready list before the new one
   * is elected. This has to be done if the running_id is !=
   * from the elected_id when start is called
   */
  if (TPL_KERN_REF(kern).running_id != TPL_KERN_REF(kern).elected_id)
  {
    tpl_put_preempted_proc((tpl_proc_id)TPL_KERN_REF(kern).elected_id);
  }
#endif

  DOW_DO(print_kern("before tpl_start"));

  TPL_KERN_REF(kern).elected_id = (uint32)proc.id;
  TPL_KERN_REF(kern).elected = tpl_dyn_proc_table[proc.id];
  TPL_KERN_REF(kern).s_elected = tpl_stat_proc_table[proc.id];

  if (TPL_KERN_REF(kern).elected->state == READY_AND_NEW)
  {
    /*
     * the object has not be preempted. So its
     * descriptor must be initialized
     */
    DOW_DO(printf("%s is a new proc\n",proc_name_table[proc.id]));
    tpl_init_proc(proc.id);
    tpl_dyn_proc_table[proc.id]->priority = proc.key;
#if NUMBER_OF_CORES > 1
    TPL_KERN_REF(kern).elected->state = (tpl_proc_state)READY;
#endif
  }

  TPL_KERN_REF(kern).need_schedule = FALSE;
  DOW_DO(print_kern("after tpl_start"));
}

/**
 * @internal
 *
 * Does the scheduling when called from a running object.
 *
 * This function is called by the OSEK/VDX Schedule
 * and ActivateTask services
 *
 */
FUNC(void, OS_CODE) tpl_schedule_from_running(CORE_ID_OR_VOID(core_id))
{
  GET_CORE_READY_LIST(core_id, ready_list)
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  VAR(uint8, AUTOMATIC) need_switch = NO_NEED_SWITCH;

  DOW_DO(print_kern("before tpl_schedule_from_running"));
  DOW_ASSERT((uint32)READY_LIST(read_list)[1].key > 0)

#if WITH_STACK_MONITORING == YES
  tpl_check_stack((tpl_proc_id)TPL_KERN_REF(kern).elected_id);
#endif /* WITH_STACK_MONITORING */

  if ((READY_LIST(ready_list)[1].key) >
      (tpl_dyn_proc_table[TPL_KERN_REF(kern).elected_id]->priority))
        {
    /* Preempts the RUNNING task */
    tpl_preempt(CORE_ID_OR_NOTHING(core_id));
    /* Starts the highest priority READY task */
    need_switch = NEED_SWITCH | NEED_SAVE;
    tpl_start(CORE_ID_OR_NOTHING(core_id));
  }

  TPL_KERN_REF(kern).need_switch = need_switch;
  DOW_DO(print_kern("after tpl_schedule_from_running"));
}

/**
 * @internal
 *
 * Terminate the RUNNING process
 *
 * This function is called by the OSEK/VDX TerminateTask, ChainTask,
 * and by the function TerminateISR
 *
 */
FUNC(void, OS_CODE) tpl_terminate(void)
{
  GET_CURRENT_CORE_ID(core_id)
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

#if WITH_STACK_MONITORING == YES
  tpl_check_stack((tpl_proc_id)TPL_KERN_REF(kern).running_id);
#endif /* WITH_STACK_MONITORING */

  DOW_DO(printf("tpl_terminate %s[%ld]\n",
    proc_name_table[TPL_KERN_REF(kern).running_id],
    TPL_KERN_REF(kern).running_id));

  /*
   * a task switch will occur. It is time to call the
   * PostTaskHook while the soon descheduled task is RUNNING
   */
  CALL_POST_TASK_HOOK()

  /*
   * the task loses the CPU because it has been put in the WAITING or
   * in the DYING state, its internal resource is released.
   */
  tpl_release_internal_resource((tpl_proc_id)TPL_KERN_REF(kern).running_id);

  /* and checked to compute its state. */
  if (TPL_KERN_REF(kern).running->activate_count > 0)
  {
    /*
     * there is at least one instance of the dying running object in
     * the ready list. So it is put in the READY_AND_NEW state. This
     * way when the next instance will be prepared to run it will
     * be initialized.
     */
    TPL_KERN_REF(kern).running->state = READY_AND_NEW;

#if EXTENDED_TASK_COUNT > 0
    /*  if the object is an extended task, init the events          */
    if (TPL_KERN_REF(kern).running_id < EXTENDED_TASK_COUNT)
    {
      CONSTP2VAR(tpl_task_events, AUTOMATIC, OS_APPL_DATA) events =
        tpl_task_events_table[TPL_KERN_REF(kern).running_id];
      events->evt_set = events->evt_wait = 0;
    }
#endif

  }
  else
  {
    /*
     * there is no instance of the dying running object in the ready
     * list. So it is put in the SUSPENDED state.
     */
    TPL_KERN_REF(kern).running->state = SUSPENDED;
  }

#if WITH_AUTOSAR_TIMING_PROTECTION == YES
  /* notify the timing protection service */
  tpl_tp_on_terminate_or_wait(TPL_KERN_REF(kern).running_id);
  tpl_tp_reset_watchdogs(TPL_KERN_REF(kern).running_id);
#endif /* WITH_AUTOSAR_TIMING_PROTECTION */

  /* copy it in old slot of tpl_kern */
/*  TPL_KERN_REF(kern).old = TPL_KERN_REF(kern).running;
  TPL_KERN_REF(kern).s_old = TPL_KERN_REF(kern).s_running;*/
}

/**
 * @internal
 *
 * Blocks the running process if needed
 *
 * This function is called by the OSEK/VDX WaitEvent
 *
 */
FUNC(void, OS_CODE) tpl_block(void)
{
  GET_CURRENT_CORE_ID(core_id)
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  DOW_DO(printf("tpl_block %s[%ld]\n",
    proc_name_table[TPL_KERN_REF(kern).running_id],
    TPL_KERN_REF(kern).running_id));

#if WITH_STACK_MONITORING == YES
  tpl_check_stack((tpl_proc_id)TPL_KERN_REF(kern).running_id);
#endif /* WITH_STACK_MONITORING == YES */

  /*
   * a task switch will occur. It is time to call the
   * PostTaskHook while the soon descheduled task is RUNNING
   */
  CALL_POST_TASK_HOOK()

  /* the task goes in the WAITING state */
  TRACE_TASK_WAIT((tpl_proc_id)TPL_KERN_REF(kern).running_id)
  TPL_KERN_REF(kern).running->state = WAITING;

  /* The internal resource is released. */
  tpl_release_internal_resource((tpl_proc_id)TPL_KERN_REF(kern).running_id);

  /* Task switching should occur */
  TPL_KERN_REF(kern).need_switch = NEED_SWITCH | NEED_SAVE;
  /* Start the highest priority task */
  tpl_start(CORE_ID_OR_NOTHING(core_id));
  LOCAL_SWITCH_CONTEXT(core_id)
}

/**
 * @internal
 *
 * TODO: document this
 */
FUNC(void, OS_CODE) tpl_start_scheduling(CORE_ID_OR_VOID(core_id))
{
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  TPL_KERN_REF(kern).need_switch = NEED_SWITCH;
  tpl_start(CORE_ID_OR_NOTHING(core_id));
}

/**
 * @internal
 *
 * This function is called by OSEK/VDX ActivateTask and ChainTask
 * and by all events that lead to the activation of a task (alarm,
 * notification, schedule table).
 *
 * the activation count is incremented
 * if the task is in the SUSPENDED state, it is moved
 * to the task list
 *
 * @param task_id   the identifier of the task
 */
FUNC(tpl_status, OS_CODE) tpl_activate_task(
  CONST(tpl_task_id, AUTOMATIC) task_id)
{
  VAR(tpl_status, AUTOMATIC)                              result = E_OS_LIMIT;
  CONSTP2VAR(tpl_proc, AUTOMATIC, OS_APPL_DATA)           task =
    tpl_dyn_proc_table[task_id];
  CONSTP2CONST(tpl_proc_static, AUTOMATIC, OS_APPL_DATA)  s_task =
    tpl_stat_proc_table[task_id];

  DOW_DO(printf("tpl_activate_task %s[%d](%d)\n",
    proc_name_table[task_id],
    task_id,
    task->priority
  ));

  if (task->activate_count < s_task->max_activate_count)
  {
#if  WITH_AUTOSAR_TIMING_PROTECTION == YES
    /* a new instance is about to be activated: we need the agreement
     of the timing protection mechanism                                */
    if (tpl_tp_on_activate_or_release(task_id) == TRUE)
    {
#endif  /* WITH_AUTOSAR_TIMING_PROTECTION */
      if (task->activate_count == 0)
      {
        GET_PROC_CORE_ID(task_id, core_id)

        /*  the initialization is postponed to the time it will
            get the CPU as indicated by READY_AND_NEW state             */
        TRACE_TASK_ACTIVATE(task_id)

        task->state = (tpl_proc_state)READY_AND_NEW;

#if EXTENDED_TASK_COUNT > 0
        /*  if the object is an extended task, init the events          */
        if (task_id < EXTENDED_TASK_COUNT)
        {
          CONSTP2VAR(tpl_task_events, AUTOMATIC, OS_APPL_DATA) events =
            tpl_task_events_table[task_id];
          events->evt_set = events->evt_wait = 0;
        }
#endif
        TPL_KERN(core_id).need_schedule = TRUE;
        /* TODO:        result = (tpl_status)E_OK_AND_SCHEDULE; */
      }

      result = E_OK;

      /*  put it in the list                                            */
      tpl_put_new_proc(task_id);
      /*  inc the task activation count. When the task will terminate
          it will dec this count and if not zero it will be reactivated */
      task->activate_count++;

#if WITH_AUTOSAR_TIMING_PROTECTION == YES
    }
    else /* timing protection forbids the activation of the instance   */
    {
      /*  OS466: If an attempt is made to activate a task before the
       end of an OsTaskTimeFrame then the Operating System module
       shall not perform the activation AND
       shall call the ProtectionHook() with E_OS_PROTECTION_ARRIVAL. */

      result = (tpl_status)E_OS_PROTECTION_ARRIVAL;
      tpl_call_protection_hook(E_OS_PROTECTION_ARRIVAL);
    }
#endif /* WITH_AUTOSAR_TIMING_PROTECTION */
  }
  return result;
}


/**
 * @internal
 *
 * This function releases a task which is in the WAITING state
 * The task become READY and is put in the ready list.
 * This function is used by tpl_set_event
 *
 * @param task_id           id of the task
 */
FUNC(void, OS_CODE) tpl_release(CONST(tpl_task_id, AUTOMATIC) task_id)
{
  GET_PROC_CORE_ID(task_id, core_id)
  CONSTP2VAR(tpl_proc, AUTOMATIC, OS_APPL_DATA) task = tpl_dyn_proc_table[task_id];
  /*  set the state to READY  */
  task->state = (tpl_proc_state)READY;
  /*  put the task in the READY list          */
  tpl_put_new_proc(task_id);
  /*  notify a scheduling needs to be done    */
  TPL_KERN(core_id).need_schedule = TRUE;
}

/**
 * @internal
 *
 * This function is used by SetEvent and by tpl_raise_alarm
 *
 * @param task_id           id of the task
 * @param incoming_event    Event mask
 */
FUNC(tpl_status, OS_CODE) tpl_set_event(
  CONST(tpl_task_id, AUTOMATIC)     task_id,
  CONST(tpl_event_mask, AUTOMATIC)  incoming_event)
{
  VAR(tpl_status, AUTOMATIC) result = E_OK;

#if EXTENDED_TASK_COUNT > 0
  CONSTP2VAR(tpl_proc, AUTOMATIC, OS_APPL_DATA) task =
    tpl_dyn_proc_table[task_id];
  CONSTP2VAR(tpl_task_events, AUTOMATIC, OS_APPL_DATA) events =
    tpl_task_events_table[task_id];

  if (task->state != (tpl_proc_state)SUSPENDED)
  {
    /*  merge the incoming event mask with the old one  */
    events->evt_set |= incoming_event;
    /*  cross check the event the task is
        waiting for and the incoming event              */
    if ((events->evt_wait & incoming_event) != 0)
    {
      /*  the task was waiting for at least one of the
          event set the wait mask is reset to 0         */
      events->evt_wait = (tpl_event_mask)0;
      /*  anyway check it is in the WAITING state       */
      if (task->state == (tpl_proc_state)WAITING)
      {
#if WITH_AUTOSAR_TIMING_PROTECTION == YES
        /* a new instance is about to be activated: we need the agreement
         of the timing protection mechanism                                */
        if (tpl_tp_on_activate_or_release(task_id) == TRUE)
        {
#endif /* WITH_AUTOSAR_TIMING_PROTECTION */
          TRACE_TASK_RELEASED(task_id,incoming_event)
          tpl_release(task_id);
#if WITH_AUTOSAR_TIMING_PROTECTION == YES
        }
        else /* timing protection forbids the activation of the instance   */
        {
          /* OS467: If an attempt is made to release a task before the
           end of an OsTaskTimeFrame then the Operating System module
           shall not perform the release AND
           shall call the ProtectionHook() with E_OS_PROTECTION_ARRIVAL. */
          result = (tpl_status)E_OS_PROTECTION_ARRIVAL;
          tpl_call_protection_hook(E_OS_PROTECTION_ARRIVAL);
        }
#endif /* WITH_AUTOSAR_TIMING_PROTECTION */
      }
    }
  }
  else
  {
    result = E_OS_STATE;
  }
#endif

  return result;
}

/**
 * @internal
 *
 * Executable object initialization.
 *
 * This function initialize the common part of task
 * or category 2 interrupt service routine to make them ready
 * for execution. If the object is an task it initializes
 * the event masks too (this has no effect on basic tasks).
 *
 * @param exec_obj address of the executable object descriptor
 */
FUNC(void, OS_CODE) tpl_init_proc(
    CONST(tpl_proc_id, AUTOMATIC) proc_id)
{
  CONSTP2VAR(tpl_proc, AUTOMATIC, OS_APPL_DATA) dyn =
    tpl_dyn_proc_table[proc_id];

  /*  set the resources list to NULL                                    */
  dyn->resources = NULL;
  /*  context init is machine dependant
      tpl_init_context is declared in tpl_machine_interface.h           */
  tpl_init_context(proc_id);
}

/**
 * @internal
 *
 * Initialization of Trampoline
 */
FUNC(void, OS_CODE) tpl_init_os(CONST(tpl_application_mode, AUTOMATIC) app_mode)
{
  GET_CURRENT_CORE_ID(core_id)
#if TASK_COUNT > 0 || ALARM_COUNT > 0 || SCHEDTABLE_COUNT > 0
  VAR(uint16, AUTOMATIC) i;
  CONST(tpl_appmode_mask, AUTOMATIC) app_mode_mask = 1 << app_mode;
#endif
  VAR(tpl_status, AUTOMATIC) result = E_OK;
#if ALARM_COUNT > 0
  P2VAR(tpl_time_obj, AUTOMATIC, OS_APPL_DATA) auto_time_obj;
#endif

#if (WITH_AUTOSAR == YES) && (ALARM_COUNT == 0) && (SCHEDTABLE_COUNT > 0)
  P2VAR(tpl_time_obj, AUTOMATIC, OS_APPL_DATA) auto_time_obj;
#endif

  /*  Start the idle task */
#if NUMBER_OF_CORES == 1
  result = tpl_activate_task(IDLE_TASK_ID);
#else
  result = tpl_activate_task(IDLE_TASK_0_ID + tpl_get_core_id());
#endif

  /*  Init the Ioc's unqueued buffer */
#if ((WITH_IOC == YES) && (IOC_UNQUEUED_COUNT > 0))
# if NUMBER_OF_CORES > 1
  /* Only one core must do this initialization */
  if (core_id == OS_CORE_ID_MASTER)
# endif
  {
    tpl_ioc_init_unqueued();
  }
#endif

#if TASK_COUNT > 0
  /*  Look for autostart tasks    */
  for (i = 0; i < TASK_COUNT; i++)
  {
    /*  each AUTOSTART task is activated if it belong to the  */
    if (tpl_task_app_mode[i] & app_mode_mask)
    {
# if NUMBER_OF_CORES > 1
      // In multicore, we must check if the task belongs to the core
      if (tpl_stat_proc_table[i]->core_id == core_id)
# endif
      {
        result = tpl_activate_task(i);
      }
    }
  }
#endif
#if ALARM_COUNT > 0

  /*  Look for autostart alarms    */

  for (i = 0; i < ALARM_COUNT; i++)
  {
    if (tpl_alarm_app_mode[i] & app_mode_mask)
    {
      auto_time_obj = (P2VAR(tpl_time_obj, AUTOMATIC, OS_APPL_DATA))tpl_alarm_table[i];
# if (NUMBER_OF_CORES > 1) && (WITH_OSAPPLICATION == YES)
      // In multicore, we must check if the alarm belongs to the core
      if (tpl_core_id_for_app[auto_time_obj->stat_part->app_id] == core_id)
# endif
      {
        auto_time_obj->state = ALARM_ACTIVE;
        tpl_insert_time_obj(auto_time_obj);
      }
    }
  }

#endif
#if (WITH_AUTOSAR == YES) && (SCHEDTABLE_COUNT > 0)
  /*  Look for autostart schedule tables  */

  for (i = 0; i < SCHEDTABLE_COUNT; i++)
  {
    if (tpl_scheduletable_app_mode[i] & app_mode_mask)
    {
      auto_time_obj =
        (P2VAR(tpl_time_obj, AUTOMATIC, OS_APPL_DATA))tpl_schedtable_table[i];
# if (NUMBER_OF_CORES > 1) && (WITH_OSAPPLICATION == YES)
      // In multicore, we must check if the schedule table belongs to the core
      if (tpl_core_id_for_app[auto_time_obj->stat_part->app_id] == core_id)
# endif
      {
        if (auto_time_obj->state == (tpl_time_obj_state)SCHEDULETABLE_AUTOSTART_RELATIVE)
        {
          auto_time_obj->state = SCHEDULETABLE_STOPPED;
          result = tpl_start_schedule_table_rel_service(i, auto_time_obj->date);
        }
        else
        {
          if (auto_time_obj->state == (tpl_time_obj_state)SCHEDULETABLE_AUTOSTART_ABSOLUTE)
          {
            auto_time_obj->state = SCHEDULETABLE_STOPPED;
            result = tpl_start_schedule_table_abs_service(i, auto_time_obj->date);
          }
#if AUTOSAR_SC == 2 || AUTOSAR_SC == 4
          else
          {
            if (auto_time_obj->state == (tpl_time_obj_state)SCHEDULETABLE_AUTOSTART_SYNCHRON)
            {
              auto_time_obj->state = SCHEDULETABLE_STOPPED;
              result = tpl_start_schedule_table_synchron_service(i);
            }
          }
#endif
        }
      }
    }
  }
#endif
}

#if TASK_COUNT > 0
FUNC(void, OS_CODE) tpl_call_terminate_task_service(void)
{
  GET_CURRENT_CORE_ID(core_id)
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  /* lock the kernel */
  LOCK_KERNEL()

  if (FALSE != tpl_get_interrupt_lock_status())
  {
    /* enable interrupts :*/
    tpl_reset_interrupt_lock_status();
    /*
     * tpl_enable_interrupts(); now ?? or wait until TerminateISR
     * reschedule and interrupts enabled returning previous API
     * service call OR by signal_handler.
     */
  }

#if RESOURCE_COUNT > 0
  /* release resources if held */
  if ((TPL_KERN_REF(kern).running->resources) != NULL){
      tpl_release_all_resources((tpl_proc_id)TPL_KERN_REF(kern).running_id);
  }
#endif

  /* error hook */
  PROCESS_ERROR(E_OS_MISSINGEND);

  /* unlock the kernel */
  UNLOCK_KERNEL()

  /* terminate the task : */
  tpl_terminate_task_service();

}
#endif

FUNC(void, OS_CODE) tpl_call_terminate_isr2_service(void)
{
  GET_CURRENT_CORE_ID(core_id)
  GET_TPL_KERN_FOR_CORE_ID(core_id, kern)

  /*  init the error to no error  */
  VAR(StatusType, AUTOMATIC) result = E_OK;

  /*  lock the task structures    */
  LOCK_KERNEL()

  /* enable interrupts if disabled */
  if (FALSE != tpl_get_interrupt_lock_status() )
  {
    tpl_reset_interrupt_lock_status();
    /*
     * tpl_enable_interrupts(); now ?? or wait until TerminateISR
     * reschedule and interrupts enabled returning previous
     * API service call OR by signal_handler.
     */
    result = E_OS_DISABLEDINT;
  }

#if RESOURCE_COUNT > 0
  /* release resources if held */
  if( (TPL_KERN_REF(kern).running->resources) != NULL ){
    tpl_release_all_resources((tpl_proc_id)TPL_KERN_REF(kern).running_id);
    result = E_OS_RESOURCE;
  }
#endif

  PROCESS_ERROR(result);  /* store terminateISR service id before hook ?*/

  tpl_terminate_isr2_service();

  /*  unlock the task structures  */
  UNLOCK_KERNEL()

}

#if NUMBER_OF_CORES > 1
/**
 * tpl_multi_schedule
 *
 * Does as many as rescheduling as indicated by need_schedule member of
 * tpl_kern structures
 */
FUNC(void, OS_CODE)tpl_multi_schedule(void)
{
  VAR(int, AUTOMATIC) core;

  for (core = 0; core < NUMBER_OF_CORES; core++)
  {
    if (tpl_kern[core]->need_schedule)
    {
      tpl_schedule_from_running(core);
    }
  }
}

/**
 * tpl_dispatch_context_switch
 *
 * Does the context switch notification to other cores. Returns TRUE
 * if the caller has to switch the context
 */
FUNC(void, OS_CODE) tpl_dispatch_context_switch(void)
{
  VAR(uint16, AUTOMATIC) caller_core = tpl_get_core_id();
  VAR(int, AUTOMATIC) core;
  for (core = 0; core < caller_core; core++)
  {
    REMOTE_SWITCH_CONTEXT(core);
  }
  for (core = caller_core + 1; core < NUMBER_OF_CORES; core++)
  {
    REMOTE_SWITCH_CONTEXT(core);
  }
}

#endif

#define OS_STOP_SEC_CODE
#include "tpl_memmap.h"

/* End of file tpl_os_kernel.c */
