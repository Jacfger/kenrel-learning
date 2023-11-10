#include "sched.h"

static int SleepPrioLUT[40] = {
	[0 ... 9] = 30,	  [10 ... 19] = 31, [20] = 32,	      [21] = 33,
	[22] = 34,	  [23] = 35,	    [24 ... 25] = 36, [26 ... 27] = 37,
	[28 ... 29] = 38, [30 ... 39] = 39,
};

void init_ts_rq(struct ts_rq *ts_rq)
{
	int i;
	for (i = 0; i < 40; ++i) {
		INIT_LIST_HEAD(&ts_rq->queue[i]);
	}
}

static void enqueue_task_ts(struct rq *rq, struct task_struct *p, int flags)
{
	/* process p is giving up CPU voluntarily*/
	if (flags & ENQUEUE_WAKEUP) {
		p->prio = 139 - SleepPrioLUT[139 - p->prio];
	}

	struct sched_ts_entity *ts_se = &p->ts;
	list_add_tail(&ts_se->list, &rq->ts.queue[139 - p->prio]);

	printk(KERN_INFO "[SCHED_TS] ENQUEUE: p->pid=%d, p->policy=%d "
			 "curr->pid=%d, curr->policy=%d, flags=%d\n",
	       p->pid, p->policy, rq->curr->pid, rq->curr->policy, flags);
}

static void dequeue_task_ts(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_ts_entity *ts_se = &p->ts;
	list_del(&ts_se->list);
	printk(KERN_INFO "[SCHED_TS] DEQUEUE: p->pid=%d, p->policy=%d "
			 "curr->pid=%d, curr->policy=%d, flags=%d\n",
	       p->pid, p->policy, rq->curr->pid, rq->curr->policy, flags);
}

static void yield_task_ts(struct rq *rq)
{
	struct sched_ts_entity *ts_se = &rq->curr->ts;
	struct ts_rq *ts_rq = &rq->ts;

	// yield the current task, put it to the end of the queue
	// Put back to the same queue
	list_move_tail(&ts_se->list, &ts_rq->queue[139 - rq->curr->prio]);

	printk(KERN_INFO "[SCHED_ts] YIELD: Process-%d, Prio %d\n",
	       rq->curr->pid, 139 - rq->curr->prio);
}

static void check_preempt_curr_ts(struct rq *rq, struct task_struct *p,
				  int flags)
{
	if (p->prio < rq->curr->prio) {
		printk(KERN_INFO
		       "[SCHED_ts] PREEMPT: Process-%d, Prio %d; by Process-%d, Prio %d\n",
		       rq->curr->pid, 139 - rq->curr->prio, p->pid,
		       139 - p->prio);
		resched_curr(rq);
	}
	return; // ts tasks are never preempted
}

static struct task_struct *
pick_next_task_ts(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	struct sched_ts_entity *ts_se = NULL;
	struct task_struct *p = NULL;
	struct ts_rq *ts_rq = &rq->ts;

	int i;
	for (i = 39; i > -1; --i) {
		if (!list_empty(&ts_rq->queue[i])) {
			put_prev_task(rq,
				      prev); // This isn't doing anything lmao
			ts_se = list_entry(ts_rq->queue[i].next,
					   struct sched_ts_entity, list);
			p = container_of(ts_se, struct task_struct, ts);
			return p;
		}
	}
	return NULL; // If you reach here then there's no task for you to choose

	// if (list_empty(&ts_rq->queue)) {
	// 	return NULL;
	// }
	// put_prev_task(rq, prev);
	// ts_se = list_entry(ts_rq->queue.next, struct sched_ts_entity, list);
	// p = container_of(ts_se, struct task_struct, ts);
	// return p;
}

static void put_prev_task_ts(struct rq *rq, struct task_struct *p)
{
}

static void set_curr_task_ts(struct rq *rq)
{
}

static void task_tick_ts(struct rq *rq, struct task_struct *p, int queued)
{
	if (p->policy != SCHED_TS)
		return;

	/* Round-robin has a time slice management here... */
	printk(KERN_INFO "[SCHED_TS] TASK TICK: Process-%d = %d\n", p->pid,
	       p->ts.time_slice);

	if (--p->ts.time_slice) // If set correctly this should never go pass 0
		return;

	int now_prio = 139 - p->prio;
	int after_prio = now_prio > 10 ? now_prio - 10 : 0;

	p->prio = 139 - after_prio;

	p->ts.time_slice = (4 - after_prio / 10) * 4;
	list_move_tail(&p->ts.list, &rq->ts.queue[after_prio]);

	printk(KERN_INFO
	       "[SCHED_TS] MOVETAIL prio: %d; Now Prio %d, Time slices %d.",
	       now_prio, after_prio, p->ts.time_slice);

	resched_curr(rq);
}

unsigned int get_rr_interval_ts(struct rq *rq, struct task_struct *p)
{
	/* Return the default time slice */
	// printk(KERN_INFO "[SCHED_TS] return time %d", );
	return (4 - ((139 - p->prio) / 10)) * 4;
}

static void prio_changed_ts(struct rq *rq, struct task_struct *p, int oldprio)
{
	return; /* ts don't support priority */
}

static void switched_to_ts(struct rq *rq, struct task_struct *p)
{
	/* nothing to do */
}

static void update_curr_ts(struct rq *rq)
{
	/* nothing to do */
}

#ifdef CONFIG_SMP
static int select_task_rq_ts(struct task_struct *p, int cpu, int sd_flag,
			     int flags)
{
	return task_cpu(p); /* ts tasks never migrate */
}
#endif

const struct sched_class ts_sched_class = {
	.next = &fair_sched_class,
	.enqueue_task = enqueue_task_ts,
	.dequeue_task = dequeue_task_ts,
	.yield_task = yield_task_ts,
	.check_preempt_curr = check_preempt_curr_ts,
	.pick_next_task = pick_next_task_ts,
	.put_prev_task = put_prev_task_ts,
	.set_curr_task = set_curr_task_ts,
	.task_tick = task_tick_ts,
	.get_rr_interval = get_rr_interval_ts,
	.prio_changed = prio_changed_ts,
	.switched_to = switched_to_ts,
	.update_curr = update_curr_ts,
#ifdef CONFIG_SMP
	.select_task_rq = select_task_rq_ts,
	.set_cpus_allowed = set_cpus_allowed_common,
#endif
};
