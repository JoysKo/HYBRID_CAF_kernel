/* Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "sync.h"
#include "oneshot_sync.h"

/**
 * struct oneshot_sync_timeline - a userspace signaled, out of order, timeline
 * @obj: base sync timeline
 * @lock: spinlock to guard other members
 * @state_list: list of oneshot_sync_states.
 * @id: next id for points creating oneshot_sync_fences
 */
struct oneshot_sync_timeline {
	struct sync_timeline obj;
	spinlock_t lock;
	struct list_head state_list;
	unsigned int id;
};

#define to_oneshot_timeline(_p) \
	container_of((_p), struct oneshot_sync_timeline, obj)

/**
 * struct oneshot_sync_state - signal state for a group of oneshot points
 * @refcount: reference count for this structure.
 * @signaled: is this signaled or not?
 * @id: identifier for this state
 * @orig_fence: fence used to create this state, no is reference count held.
 * @timeline: back pointer to the timeline.
 */
struct oneshot_sync_state {
	struct kref refcount;
	struct list_head node;
	bool signaled;
	unsigned int id;
	struct sync_file *orig_sync_file;
	struct oneshot_sync_timeline *timeline;
};

/**
 * struct oneshot_sync_fence
 * @sync_pt: base sync point structure
 * @state: reference counted pointer to the state of this pt
 */
struct oneshot_sync_fence {
	struct fence fence;
	struct oneshot_sync_state *state;
	bool dup;
};
#define to_oneshot_pt(_p) container_of((_p), struct oneshot_sync_fence, fence)

static void oneshot_state_destroy(struct kref *ref)
{
	struct oneshot_sync_state *state =
		container_of(ref, struct oneshot_sync_state, refcount);

	spin_lock(&state->timeline->lock);
	list_del(&state->node);
	spin_unlock(&state->timeline->lock);

	kfree(state);
}

static void oneshot_state_put(struct oneshot_sync_state *state)
{
	kref_put(&state->refcount, oneshot_state_destroy);
}

static struct oneshot_sync_fence *
oneshot_pt_create(struct oneshot_sync_timeline *timeline)
{
	struct oneshot_sync_fence *pt = NULL;

	pt = (struct oneshot_sync_fence *)sync_pt_create(&timeline->obj,
						     sizeof(*pt));
	if (pt == NULL)
		return NULL;

	pt->state = kzalloc(sizeof(struct oneshot_sync_state), GFP_KERNEL);
	if (pt->state == NULL)
		goto error;

	kref_init(&pt->state->refcount);
	pt->state->signaled = false;
	pt->state->timeline = timeline;

	spin_lock(&timeline->lock);
	/* assign an id to the state, which could be shared by several pts. */
	pt->state->id = ++(timeline->id);
	/* add this pt to the list of pts that can be signaled by userspace */
	list_add_tail(&pt->state->node, &timeline->state_list);
	spin_unlock(&timeline->lock);

	return pt;
error:
	if (pt)
		fence_put(&pt->fence);
	return NULL;
}

static int oneshot_pt_has_signaled(struct fence *fence)
{
	struct oneshot_sync_fence *pt = to_oneshot_pt(fence);

	return pt->state->signaled;
}

static void oneshot_fence_value_str(struct fence *fence, char *str, int size)
{
	struct oneshot_sync_fence *pt = to_oneshot_pt(fence);

	snprintf(str, size, "%u", pt->state->id);
}

static struct sync_timeline_ops oneshot_timeline_ops = {
	.driver_name = "oneshot",
	.has_signaled = oneshot_pt_has_signaled,
	.fence_value_str = oneshot_fence_value_str,
};

struct oneshot_sync_timeline *oneshot_timeline_create(const char *name)
{
	struct oneshot_sync_timeline *timeline = NULL;
	static const char *default_name = "oneshot-timeline";

	if (name == NULL)
		name = default_name;

	timeline = (struct oneshot_sync_timeline *)
			sync_timeline_create(&oneshot_timeline_ops,
					     sizeof(*timeline),
					     name);

	if (timeline == NULL)
		return NULL;

	INIT_LIST_HEAD(&timeline->state_list);
	spin_lock_init(&timeline->lock);

	return timeline;
}
EXPORT_SYMBOL(oneshot_timeline_create);

void oneshot_timeline_destroy(struct oneshot_sync_timeline *timeline)
{
	if (timeline)
		sync_timeline_destroy(&timeline->obj);
}
EXPORT_SYMBOL(oneshot_timeline_destroy);

struct sync_file *oneshot_fence_create(struct oneshot_sync_timeline *timeline,
					const char *name)
{
	struct sync_file *sync_file = NULL;
	struct oneshot_sync_fence *pt = NULL;

	pt = oneshot_pt_create(timeline);
	if (pt == NULL)
		return NULL;

	sync_file = sync_file_create(name, &pt->fence);
	if (sync_file == NULL) {
		fence_put(&pt->fence);
		return NULL;
	}

	pt->state->orig_sync_file = sync_file;

	return sync_file;
}
EXPORT_SYMBOL(oneshot_fence_create);

int oneshot_fence_signal(struct oneshot_sync_timeline *timeline,
			struct sync_file *sync_file)
{
	int ret = -EINVAL;
	struct oneshot_sync_state *state = NULL;
	bool signaled = false;

	if (timeline == NULL || sync_file == NULL)
		return -EINVAL;

	spin_lock(&timeline->lock);
	list_for_each_entry(state, &timeline->state_list, node) {
		/*
		 * If we have the point from this fence on our list,
		 * this is is the original fence we created, so signal it.
		 */
		if (state->orig_sync_file == sync_file) {
			/* ignore attempts to signal multiple times */
			if (!state->signaled) {
				state->signaled = true;
				signaled = true;
			}
			ret = 0;
			break;
		}
	}
	spin_unlock(&timeline->lock);
	if (ret == -EINVAL)
		pr_debug("fence: %pK not from this timeline\n", sync_file);

	if (signaled)
		sync_timeline_signal(&timeline->obj);
	return ret;
}
EXPORT_SYMBOL(oneshot_fence_signal);

#ifdef CONFIG_ONESHOT_SYNC_USER

static int oneshot_open(struct inode *inode, struct file *file)
{
	struct oneshot_sync_timeline *timeline = NULL;
	char name[32];
	char task_comm[TASK_COMM_LEN];

	get_task_comm(task_comm, current);
	snprintf(name, sizeof(name), "%s-oneshot", task_comm);

	timeline = oneshot_timeline_create(name);
	if (timeline == NULL)
		return -ENOMEM;

	file->private_data = timeline;
	return 0;
}

static int oneshot_release(struct inode *inode, struct file *file)
{
	struct oneshot_sync_timeline *timeline = file->private_data;

	oneshot_timeline_destroy(timeline);

	return 0;
}

static long oneshot_ioctl_fence_create(struct oneshot_sync_timeline *timeline,
				 unsigned long arg)
{
	struct oneshot_sync_create_fence param;
	int ret = -ENOMEM;
	struct sync_file *sync_file = NULL;
	int fd = get_unused_fd_flags(0);

	if (fd < 0)
		return fd;

	if (copy_from_user(&param, (void __user *)arg, sizeof(param))) {
		ret = -EFAULT;
		goto out;
	}

	sync_file = oneshot_fence_create(timeline, param.name);
	if (sync_file == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	param.fence_fd = fd;

	if (copy_to_user((void __user *)arg, &param, sizeof(param))) {
		ret = -EFAULT;
		goto out;
	}

	sync_fence_install(sync_file, fd);
	ret = 0;
out:
	if (ret) {
		if (sync_file)
			sync_file_put(sync_file);
		put_unused_fd(fd);
	}
	return ret;
}



static long oneshot_ioctl_fence_signal(struct oneshot_sync_timeline *timeline,
				 unsigned long arg)
{
	int ret = -EINVAL;
	int fd = -1;
	struct sync_file *sync_file = NULL;

	if (get_user(fd, (int __user *)arg))
		return -EFAULT;

	sync_file = sync_file_fdget(fd);
	if (sync_file == NULL)
		return -EBADF;

	ret = oneshot_fence_signal(timeline, fence);
	sync_file_put(sync_file);

	return ret;
}

static long oneshot_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct oneshot_sync_timeline *timeline = file->private_data;

	switch (cmd) {
	case ONESHOT_SYNC_IOC_CREATE_FENCE:
		return oneshot_ioctl_fence_create(timeline, arg);

	case ONESHOT_SYNC_IOC_SIGNAL_FENCE:
		return oneshot_ioctl_fence_signal(timeline, arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations oneshot_fops = {
	.owner = THIS_MODULE,
	.open = oneshot_open,
	.release = oneshot_release,
	.unlocked_ioctl = oneshot_ioctl,
	.compat_ioctl = oneshot_ioctl,
};
static struct miscdevice oneshot_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "oneshot_sync",
	.fops	= &oneshot_fops,
};

static int __init oneshot_init(void)
{
	return misc_register(&oneshot_dev);
}

static void __exit oneshot_remove(void)
{
	misc_deregister(&oneshot_dev);
}

module_init(oneshot_init);
module_exit(oneshot_remove);

#endif /* CONFIG_ONESHOT_SYNC_USER */
MODULE_LICENSE("GPL v2");

