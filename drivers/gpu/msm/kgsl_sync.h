/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
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
#ifndef __KGSL_SYNC_H
#define __KGSL_SYNC_H

#include "sync.h"
#include "kgsl_device.h"

struct kgsl_sync_timeline {
	struct sync_timeline timeline;
	unsigned int last_timestamp;
	struct kgsl_device *device;
	u32 context_id;
	spinlock_t lock;
};

struct kgsl_sync_fence {
	struct fence pt;
	unsigned int timestamp;
};

struct sync_file_waiter;
typedef void (*sync_callback_t)(struct sync_file *sync_file,
				struct sync_file_waiter *waiter);

/**
 * struct sync_file_waiter - metadata for asynchronous waiter on a fence
 * @work:		wait_queue for the fence waiter
 * @callback:		function pointer to call when fence signals
 */
struct sync_file_waiter {
	wait_queue_t work;
	sync_callback_t callback;
};

struct kgsl_sync_fence_waiter {
	struct sync_file_waiter waiter;
	struct sync_file *sync_file;
	char name[32];
	void (*func)(void *priv);
	void *priv;
};

/**
 * sync_fence_wait_async() - registers and async wait on the fence
 * @fence:		fence to wait on
 * @waiter:		waiter callback struck
 *
 * Registers a callback to be called when @fence signals or has an error.
 * @waiter should be initialized with sync_fence_waiter_init().
 *
 * Returns 1 if @fence has already signaled, 0 if not or <0 if error.
 */
static int sync_fence_wait_async(struct sync_file *sync_file,
			  struct sync_file_waiter *waiter);

/**
 * sync_fence_cancel_async() - cancels an async wait
 * @fence:		fence to wait on
 * @waiter:		waiter callback struck
 *
 * Cancels a previously registered async wait.  Will fail gracefully if
 * @waiter was never registered or if @fence has already signaled @waiter.
 *
 * Returns 0 if waiter was removed from fence's async waiter list.
 * Returns -ENOENT if waiter was not found on fence's async waiter list.
 */
static int sync_fence_cancel_async(struct sync_file *sync_file,
			    struct sync_file_waiter *waiter);

struct kgsl_syncsource;

#if defined(CONFIG_SYNC)
int kgsl_add_fence_event(struct kgsl_device *device,
	u32 context_id, u32 timestamp, void __user *data, int len,
	struct kgsl_device_private *owner);
int kgsl_sync_timeline_create(struct kgsl_context *context);
void kgsl_sync_timeline_destroy(struct kgsl_context *context);
struct kgsl_sync_fence_waiter *kgsl_sync_fence_async_wait(int fd,
	void (*func)(void *priv), void *priv);
int kgsl_sync_fence_async_cancel(struct kgsl_sync_fence_waiter *waiter);
static inline void kgsl_sync_fence_log(struct sync_file *sync_file)
{
}
#else
static inline int kgsl_add_fence_event(struct kgsl_device *device,
	u32 context_id, u32 timestamp, void __user *data, int len,
	struct kgsl_device_private *owner)
{
	return -EINVAL;
}

static inline int kgsl_sync_timeline_create(struct kgsl_context *context)
{
	context->timeline = NULL;
	return 0;
}

static inline void kgsl_sync_timeline_destroy(struct kgsl_context *context)
{
}

static inline struct
kgsl_sync_fence_waiter *kgsl_sync_fence_async_wait(int fd,
	void (*func)(void *priv), void *priv)
{
	return NULL;
}

static inline int
kgsl_sync_fence_async_cancel(struct kgsl_sync_fence_waiter *waiter)
{
	return 1;
}

static inline void kgsl_sync_fence_log(struct sync_file *sync_file)
{
}

#endif

#ifdef CONFIG_ONESHOT_SYNC
long kgsl_ioctl_syncsource_create(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data);
long kgsl_ioctl_syncsource_destroy(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data);
long kgsl_ioctl_syncsource_create_fence(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data);
long kgsl_ioctl_syncsource_signal_fence(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data);

void kgsl_syncsource_put(struct kgsl_syncsource *syncsource);

#else
static inline long
kgsl_ioctl_syncsource_create(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data)
{
	return -ENOIOCTLCMD;
}

static inline long
kgsl_ioctl_syncsource_destroy(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data)
{
	return -ENOIOCTLCMD;
}

static inline long
kgsl_ioctl_syncsource_create_fence(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data)
{
	return -ENOIOCTLCMD;
}

static inline long
kgsl_ioctl_syncsource_signal_fence(struct kgsl_device_private *dev_priv,
					unsigned int cmd, void *data)
{
	return -ENOIOCTLCMD;
}

static inline void kgsl_syncsource_put(struct kgsl_syncsource *syncsource)
{

}
#endif

static int sync_fence_wake_up_wq(wait_queue_t *curr, unsigned mode,
			  int wake_flags, void *key)
{
	struct sync_file_waiter *wait;

	wait = container_of(curr, struct sync_file_waiter, work);
	list_del_init(&wait->work.task_list);

	wait->callback(wait->work.private, wait);
	return 1;
}

static int sync_fence_wait_async(struct sync_file *sync_file,
			  struct sync_file_waiter *waiter)
{
	int err = atomic_read(&sync_file->status);
	unsigned long flags;

	if (err < 0)
		return err;

	if (!err)
		return 1;

	init_waitqueue_func_entry(&waiter->work, sync_fence_wake_up_wq);
	waiter->work.private = sync_file;

	spin_lock_irqsave(&sync_file->wq.lock, flags);
	err = atomic_read(&sync_file->status);
	if (err > 0)
		__add_wait_queue_tail(&sync_file->wq, &waiter->work);
	spin_unlock_irqrestore(&sync_file->wq.lock, flags);

	if (err < 0)
		return err;

	return !err;
}
EXPORT_SYMBOL(sync_fence_wait_async);

static int sync_fence_cancel_async(struct sync_file *sync_file,
			    struct sync_file_waiter *waiter)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&sync_file->wq.lock, flags);
	if (!list_empty(&waiter->work.task_list))
		list_del_init(&waiter->work.task_list);
	else
		ret = -ENOENT;
	spin_unlock_irqrestore(&sync_file->wq.lock, flags);
	return ret;
}
EXPORT_SYMBOL(sync_fence_cancel_async);

#endif /* __KGSL_SYNC_H */
