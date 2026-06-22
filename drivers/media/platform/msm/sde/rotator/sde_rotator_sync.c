/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <sync.h>
#include <sw_sync.h>

#include "sde_rotator_util.h"
#include "sde_rotator_sync.h"

struct sde_rot_timeline {
	struct mutex lock;
	struct sw_sync_timeline *timeline;
	u32 next_value;
	char sync_file_name[32];
};
/*
 * sde_rotator_create_timeline - Create timeline object with the given name
 * @name: Pointer to name character string.
 */
struct sde_rot_timeline *sde_rotator_create_timeline(const char *name)
{
	char tl_name[32];
	struct sde_rot_timeline *tl;

	if (!name) {
		SDEROT_ERR("invalid parameters\n");
		return NULL;
	}

	tl = kzalloc(sizeof(struct sde_rot_timeline), GFP_KERNEL);
	if (!tl)
		return NULL;

	snprintf(tl_name, sizeof(tl_name), "rot_timeline_%s", name);
	SDEROT_DBG("timeline name=%s\n", tl_name);
	tl->timeline = sw_sync_timeline_create(tl_name);
	if (!tl->timeline) {
		SDEROT_ERR("fail to allocate timeline\n");
		kfree(tl);
		return NULL;
	}

	snprintf(tl->sync_file_name, sizeof(tl->sync_file_name), "rot_sync_file_%s", name);
	mutex_init(&tl->lock);
	tl->next_value = 0;

	return tl;
}

/*
 * sde_rotator_destroy_timeline - Destroy the given timeline object
 * @tl: Pointer to timeline object.
 */
void sde_rotator_destroy_timeline(struct sde_rot_timeline *tl)
{
	if (!tl) {
		SDEROT_ERR("invalid parameters\n");
		return;
	}

	if (tl->timeline)
		sync_timeline_destroy((struct sync_timeline *) tl->timeline);

	kfree(tl);
}

/*
 * sde_rotator_resync_timeline - Resync timeline to last committed value
 * @tl: Pointer to timeline object.
 */
void sde_rotator_resync_timeline(struct sde_rot_timeline *tl)
{
	int val;

	if (!tl || !tl->timeline) {
		SDEROT_ERR("invalid parameters\n");
		return;
	}
	mutex_lock(&tl->lock);
	val = tl->next_value - tl->timeline->value;
	if (val > 0) {
		SDEROT_WARN("flush %s:%d\n", tl->sync_file_name, val);
		sw_sync_timeline_inc(tl->timeline, val);
	}
	mutex_unlock(&tl->lock);
}

/*
 * sde_rotator_get_sync_file - Create sync_file object from the given timeline
 * @tl: Pointer to timeline object
 * @sync_file_fd: Pointer to file descriptor associated with the returned sync_file.
 *		Null if not required.
 * @timestamp: Pointer to timestamp of the returned sync_file. Null if not required.
 */
struct sde_rot_sync_file *sde_rotator_get_sync_file(
		struct sde_rot_timeline *tl, int *sync_file_fd,
		u32 *timestamp)
{
	u32 val;
	struct fence *sync_pt;
	struct sync_file *sync_file;

	if (!tl || !tl->timeline) {
		SDEROT_ERR("invalid parameters\n");
		return NULL;
	}

	mutex_lock(&tl->lock);
	val = tl->next_value + 1;

	sync_pt = sw_sync_fence_create(tl->timeline, val);
	if (sync_pt == NULL) {
		SDEROT_ERR("cannot create sync point\n");
		goto sync_pt_create_err;
	}

	/* create sync_file */
	sync_file = sync_file_create(tl->sync_file_name, sync_pt);
	if (sync_file == NULL) {
		SDEROT_ERR("%s: cannot create sync_file\n",
				tl->sync_file_name);
		goto sync_file_create_err;
	}

	if (sync_file_fd) {
		int fd = get_unused_fd_flags(0);

		if (fd < 0) {
			SDEROT_ERR("get_unused_fd_flags failed error:0x%x\n",
					fd);
			goto get_fd_err;
		}

		sync_file_install(sync_file, fd);
		*sync_file_fd = fd;
	}

	if (timestamp)
		*timestamp = val;

	tl->next_value++;
	mutex_unlock(&tl->lock);
	SDEROT_DBG("output sync point created at val=%u\n", val);

	return (struct sde_rot_sync_file *) sync_file;
get_fd_err:
	SDEROT_DBG("sys_sync_file_put c:%p\n", sync_file);
	sync_file_put(sync_file);
sync_file_create_err:
	fence_put(sync_pt);
sync_pt_create_err:
	mutex_unlock(&tl->lock);
	return NULL;
}

/*
 * sde_rotator_inc_timeline - Increment timeline by given amount
 * @tl: Pointer to timeline object.
 * @increment: the amount to increase the timeline by.
 */
int sde_rotator_inc_timeline(struct sde_rot_timeline *tl, int increment)
{
	if (!tl || !tl->timeline) {
		SDEROT_ERR("invalid parameters\n");
		return -EINVAL;
	}

	mutex_lock(&tl->lock);
	sw_sync_timeline_inc(tl->timeline, increment);
	mutex_unlock(&tl->lock);

	return 0;
}

/*
 * sde_rotator_get_timeline_commit_ts - Return commit tick of given timeline
 * @tl: Pointer to timeline object.
 */
u32 sde_rotator_get_timeline_commit_ts(struct sde_rot_timeline *tl)
{
	if (!tl)
		return 0;

	return tl->next_value;
}

/*
 * sde_rotator_get_timeline_retire_ts - Return retire tick of given timeline
 * @tl: Pointer to timeline object.
 */
u32 sde_rotator_get_timeline_retire_ts(struct sde_rot_timeline *tl)
{
	if (!tl || !tl->timeline) {
		SDEROT_ERR("invalid parameters\n");
		return 0;
	}

	return tl->timeline->value;
}

/*
 * sde_rotator_put_sync_file - Destroy given sync_file object
 * @sync_file: Pointer to sync_file object.
 */
void sde_rotator_put_sync_file(struct sde_rot_sync_file *sync_file)
{
	if (!sync_file) {
		SDEROT_ERR("invalid parameters\n");
		return;
	}

	sync_file_put((struct sync_file *) sync_file);
}

static int sync_file_wait(struct sync_file *sync_file, long timeout)
{
	long ret;

	if (timeout < 0)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else
		timeout = msecs_to_jiffies(timeout);

	ret = wait_event_interruptible_timeout(sync_file->wq,
					       atomic_read(&sync_file->status) <= 0,
					       timeout);

	if (ret < 0) {
		return ret;
	} else if (ret == 0) {
		if (timeout) {
			pr_info("sync_file timeout on [%p] after %dms\n",
				sync_file, jiffies_to_msecs(timeout));
			if (jiffies_to_msecs(timeout) >= 7000)
				sync_dump();
		}
		return -ETIME;
	}

	ret = atomic_read(&sync_file->status);
	if (ret) {
		pr_info("sync_file error %ld on [%p]\n", ret, sync_file);
		sync_dump();
	}
	return ret;
}

/*
 * sde_rotator_wait_sync_file - Wait until sync_file signal or timeout
 * @sync_file: Pointer to sync_file object.
 * @timeout: maximum wait time, in msec, for sync_file to signal.
 */
int sde_rotator_wait_sync_file(struct sde_rot_sync_file *sync_file,
		long timeout)
{
	if (!sync_file)
		return -EINVAL;

	return sync_file_wait((struct sync_file *) sync_file, timeout);
}

/*
 * sde_rotator_get_sync_file_fd - Get sync_file object of given file descriptor
 * @fd: File description of sync_file object.
 */
struct sde_rot_sync_file *sde_rotator_get_fd_sync_file(int fd)
{
	return (struct sde_rot_sync_file *) sync_file_fdget(fd);
}

/*
 * sde_rotator_get_sync_file_fd - Get file descriptor of given sync_file object
 * @sync_file: Pointer to sync_file object.
 */
int sde_rotator_get_sync_file_fd(struct sde_rot_sync_file *sync_file)
{
	int fd;

	if (!sync_file) {
		SDEROT_ERR("invalid parameters\n");
		return -EINVAL;
	}

	fd = get_unused_fd_flags(0);

	if (fd < 0) {
		SDEROT_ERR("fail to get unused fd\n");
		return fd;
	}

	sync_file_install((struct sync_file *) sync_file, fd);

	return fd;
}

