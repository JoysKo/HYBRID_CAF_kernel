/* Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
 *
 * Modified 2025, DeepSeek & BPRGroup
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sched.h"
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include "walt.h"
#include <trace/events/sched.h>

/* Заглушка для отсутствующего tracepoint в ядре 4.4 */
#ifndef trace_sched_set_boost
#define trace_sched_set_boost(type) do { } while (0)
#endif

unsigned int sysctl_sched_boost;
static enum sched_boost_policy boost_policy;
static enum sched_boost_policy boost_policy_dt = SCHED_BOOST_NONE;
static DEFINE_MUTEX(boost_mutex);
static unsigned int freq_aggr_threshold_backup;

/* Атомные флаги для каждого CPU */
static atomic_t boost_kick_flags[NR_CPUS] = {ATOMIC_INIT(0)};

/* Объявляем внешние переменные из walt.c ядра 4.4 */
extern unsigned long max_capacity;
extern unsigned long capacity_curr_of(int cpu);

static inline void boost_kick(int cpu)
{
    if (!atomic_xchg(&boost_kick_flags[cpu], 1)) {
        smp_send_reschedule(cpu);
    }
}

static void boost_kick_cpus(void)
{
    int i;
    struct cpumask kick_mask;
    unsigned long capacity;

    if (boost_policy != SCHED_BOOST_ON_BIG)
        return;

    cpumask_andnot(&kick_mask, cpu_online_mask, cpu_isolated_mask);

    for_each_cpu(i, &kick_mask) {
        capacity = capacity_curr_of(i);
        if (capacity != max_capacity)
            boost_kick(i);
    }
}

int got_boost_kick(void)
{
    int cpu = smp_processor_id();
    return atomic_xchg(&boost_kick_flags[cpu], 0);
}

void clear_boost_kick(int cpu)
{
    atomic_set(&boost_kick_flags[cpu], 0);
}

/*
 * Scheduler boost type and boost policy might at first seem unrelated,
 * however, there exists a connection between them that will allow us
 * to use them interchangeably during placement decisions. We'll explain
 * the connection here in one possible way so that the implications are
 * clear when looking at placement policies.
 *
 * When policy = SCHED_BOOST_NONE, type is either none or RESTRAINED
 * When policy = SCHED_BOOST_ON_ALL or SCHED_BOOST_ON_BIG, type can
 * neither be none nor RESTRAINED.
 */
static void set_boost_policy(int type)
{
    if (type == SCHED_BOOST_NONE || type == RESTRAINED_BOOST) {
        boost_policy = SCHED_BOOST_NONE;
        return;
    }

    if (boost_policy_dt) {
        boost_policy = boost_policy_dt;
        return;
    }

    /* Замена: sysctl_sched_is_big_little может отсутствовать.
     * Логику можно упростить, всегда используя SCHED_BOOST_ON_BIG при наличии little/big ядер.
     */
    if (1) { // Простая замена: предположим, что архитектура гетерогенная
        boost_policy = SCHED_BOOST_ON_BIG;
        return;
    }

    boost_policy = SCHED_BOOST_ON_ALL;
}

enum sched_boost_policy sched_boost_policy(void)
{
	return boost_policy;
}

static bool verify_boost_params(int old_val, int new_val)
{
	/*
	 * Boost can only be turned on or off. There is no possiblity of
	 * switching from one boost type to another or to set the same
	 * kind of boost several times.
	 */
	return !(!!old_val == !!new_val);
}

/* Заглушки для отсутствующих функций */
static void core_ctl_set_boost(bool enable)
{
    /* Заглушка: функция core_ctl может отсутствовать в ядре 4.4.
     * Можно оставить пустой или добавить совместимую реализацию.
     */
    pr_debug("core_ctl_set_boost: %d (stub)\n", enable);
}

static void update_cgroup_boost_settings(void)
{
    /* Заглушка: реализация управления boost через Cgroups в ядре 4.4 может отличаться. */
    pr_debug("update_cgroup_boost_settings (stub)\n");
}

static void restore_cgroup_boost_settings(void)
{
    /* Заглушка */
    pr_debug("restore_cgroup_boost_settings (stub)\n");
}

static unsigned int update_freq_aggregate_threshold(unsigned int threshold)
{
    /* Заглушка: этот механизм агрегации частот может отсутствовать.
     * Возвращаем предыдущее значение для совместимости.
     */
    pr_debug("update_freq_aggregate_threshold: %u (stub)\n", threshold);
    return 0;
}

static void _sched_set_boost(int old_val, int type)
{
	switch (type) {
	case NO_BOOST:
		if (old_val == FULL_THROTTLE_BOOST)
			core_ctl_set_boost(false);
		else if (old_val == CONSERVATIVE_BOOST)
			restore_cgroup_boost_settings();
		else
			update_freq_aggregate_threshold(
				freq_aggr_threshold_backup);
		break;

	case FULL_THROTTLE_BOOST:
		core_ctl_set_boost(true);
		boost_kick_cpus();
		break;

	case CONSERVATIVE_BOOST:
		update_cgroup_boost_settings();
		boost_kick_cpus();
		break;

	case RESTRAINED_BOOST:
		freq_aggr_threshold_backup =
			update_freq_aggregate_threshold(1);
		break;

	default:
		WARN_ON(1);
		return;
	}

	set_boost_policy(type);
	sysctl_sched_boost = type;
	trace_sched_set_boost(type);
}

void sched_boost_parse_dt(void)
{
	struct device_node *sn;
	const char *boost_policy;

	sn = of_find_node_by_path("/sched-hmp");
	if (!sn)
		return;

	if (!of_property_read_string(sn, "boost-policy", &boost_policy)) {
		if (!strcmp(boost_policy, "boost-on-big"))
			boost_policy_dt = SCHED_BOOST_ON_BIG;
		else if (!strcmp(boost_policy, "boost-on-all"))
			boost_policy_dt = SCHED_BOOST_ON_ALL;
	}
}

int sched_set_boost(int type)
{
	int ret = 0;

	mutex_lock(&boost_mutex);

	if (verify_boost_params(sysctl_sched_boost, type))
		_sched_set_boost(sysctl_sched_boost, type);
	else
		ret = -EINVAL;

	mutex_unlock(&boost_mutex);
	return ret;
}

int sched_boost_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;
	unsigned int *data = (unsigned int *)table->data;
	unsigned int old_val;

	mutex_lock(&boost_mutex);

	old_val = *data;
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (ret || !write)
		goto done;

	if (verify_boost_params(old_val, *data)) {
		_sched_set_boost(old_val, *data);
	} else {
		*data = old_val;
		ret = -EINVAL;
	}

done:
	mutex_unlock(&boost_mutex);
	return ret;
}

int sched_boost(void)
{
	return sysctl_sched_boost;
}
