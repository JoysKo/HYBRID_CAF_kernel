/*
 * RCU expedited grace periods
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2016
 *
 * Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

/* Let the workqueue handler know what it is supposed to do. */
struct rcu_exp_work {
	smp_call_func_t rew_func;
	struct rcu_state *rew_rsp;
	unsigned long rew_s;
	struct work_struct rew_work;
};

/*
 * Work-queue handler to drive an expedited grace period forward.
 */
static void wait_rcu_exp_gp(struct work_struct *wp)
{
	struct rcu_exp_work *rewp;

	/* Initialize the rcu_node tree in preparation for the wait. */
	rewp = container_of(wp, struct rcu_exp_work, rew_work);
	sync_rcu_exp_select_cpus(rewp->rew_rsp, rewp->rew_func);

	/* Wait and clean up, including waking everyone. */
	rcu_exp_wait_wake(rewp->rew_rsp, rewp->rew_s);
}

/*
 * Given an rcu_state pointer and a smp_call_function() handler, kick
 * off the specified flavor of expedited grace period.
 */
static void _synchronize_rcu_expedited(struct rcu_state *rsp,
				       smp_call_func_t func)
{
	struct rcu_data *rdp;
	struct rcu_exp_work rew;
	struct rcu_node *rnp;
	unsigned long s;

	/* If expedited grace periods are prohibited, fall back to normal. */
	if (rcu_gp_is_normal()) {
		wait_rcu_gp(rsp->call);
		return;
	}

	/* Take a snapshot of the sequence number.  */
	s = rcu_exp_gp_seq_snap(rsp);
	if (exp_funnel_lock(rsp, s))
		return;  /* Someone else did our work for us. */

	/* Marshall arguments and schedule the expedited grace period. */
	rew.rew_func = func;
	rew.rew_rsp = rsp;
	rew.rew_s = s;
	INIT_WORK_ONSTACK(&rew.rew_work, wait_rcu_exp_gp);
	schedule_work(&rew.rew_work);

	/* Wait for expedited grace period to complete. */
	rdp = per_cpu_ptr(rsp->rda, raw_smp_processor_id());
	rnp = rcu_get_root(rsp);
	wait_event(rnp->exp_wq[(s >> 1) & 0x3],
           sync_exp_work_done(rsp,
                              &rnp->exp_seq_rq, s));

	/* Let the next expedited grace period start. */
	mutex_unlock(&rsp->exp_mutex);
}
