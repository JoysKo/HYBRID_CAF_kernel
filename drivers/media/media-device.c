/*
 * Media device
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* We need to access legacy defines from linux/media.h */
#define __NEED_MEDIA_LEGACY_API

#include <linux/compat.h>
#include <linux/export.h>
#include <linux/ioctl.h>
#include <linux/media.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/usb.h>

#include <media/media-device.h>
#include <media/media-devnode.h>
#include <media/media-entity.h>

/* -----------------------------------------------------------------------------
 * Userspace API
 */

static int media_device_open(struct file *filp)
{
	return 0;
}

static int media_device_close(struct file *filp)
{
	return 0;
}

static int media_device_get_info(struct media_device *dev,
				 struct media_device_info __user *__info)
{
	struct media_device_info info;

	memset(&info, 0, sizeof(info));

	strlcpy(info.driver, dev->dev->driver->name, sizeof(info.driver));
	strlcpy(info.model, dev->model, sizeof(info.model));
	strlcpy(info.serial, dev->serial, sizeof(info.serial));
	strlcpy(info.bus_info, dev->bus_info, sizeof(info.bus_info));

	info.media_version = MEDIA_API_VERSION;
	info.hw_revision = dev->hw_revision;
	info.driver_version = dev->driver_version;

	if (copy_to_user(__info, &info, sizeof(*__info)))
		return -EFAULT;
	return 0;
}

static struct media_entity *find_entity(struct media_device *mdev, u32 id)
{
	struct media_entity *entity;
	int next = id & MEDIA_ENT_ID_FLAG_NEXT;

	id &= ~MEDIA_ENT_ID_FLAG_NEXT;

	spin_lock(&mdev->lock);

	media_device_for_each_entity(entity, mdev) {
		if ((entity->id == id && !next) ||
		    (entity->id > id && next)) {
			spin_unlock(&mdev->lock);
			return entity;
		}
	}

	spin_unlock(&mdev->lock);

	return NULL;
}

static long media_device_enum_entities(struct media_device *mdev,
				       struct media_entity_desc __user *uent)
{
	struct media_entity *ent;
	struct media_entity_desc u_ent;

	memset(&u_ent, 0, sizeof(u_ent));
	if (copy_from_user(&u_ent.id, &uent->id, sizeof(u_ent.id)))
		return -EFAULT;

	ent = find_entity(mdev, u_ent.id);

	if (ent == NULL)
		return -EINVAL;

	u_ent.id = ent->id;
	if (ent->name)
		strlcpy(u_ent.name, ent->name, sizeof(u_ent.name));
	u_ent.type = ent->type;
	u_ent.revision = ent->revision;
	u_ent.flags = ent->flags;
	u_ent.group_id = ent->group_id;
	u_ent.pads = ent->num_pads;
	u_ent.links = ent->num_links - ent->num_backlinks;

	/*
	 * Workaround for a bug at media-ctl <= v1.10 that makes it to
	 * do the wrong thing if the entity function doesn't belong to
	 * either MEDIA_ENT_F_OLD_BASE or MEDIA_ENT_F_OLD_SUBDEV_BASE
	 * Ranges.
	 *
	 * Non-subdevices are expected to be at the MEDIA_ENT_F_OLD_BASE,
	 * or, otherwise, will be silently ignored by media-ctl when
	 * printing the graphviz diagram. So, map them into the devnode
	 * old range.
	 */
	if (ent->type < MEDIA_ENT_F_OLD_BASE ||
	    ent->type > MEDIA_ENT_T_DEVNODE_UNKNOWN) {
		if (is_media_entity_v4l2_subdev(ent))
			u_ent.type = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;
		else if (ent->type != MEDIA_ENT_F_IO_V4L)
			u_ent.type = MEDIA_ENT_T_DEVNODE_UNKNOWN;
	}

	memcpy(&u_ent.raw, &ent->info, sizeof(ent->info));
	if (copy_to_user(uent, &u_ent, sizeof(u_ent)))
		return -EFAULT;
	return 0;
}

static void media_device_kpad_to_upad(const struct media_pad *kpad,
				      struct media_pad_desc *upad)
{
	upad->entity = kpad->entity->id;
	upad->index = kpad->index;
	upad->flags = kpad->flags;
}

static long __media_device_enum_links(struct media_device *mdev,
				      struct media_links_enum *links)
{
	struct media_entity *entity;

	entity = find_entity(mdev, links->entity);
	if (entity == NULL)
		return -EINVAL;

	if (links->pads) {
		unsigned int p;

		for (p = 0; p < entity->num_pads; p++) {
			struct media_pad_desc pad;

			memset(&pad, 0, sizeof(pad));
			media_device_kpad_to_upad(&entity->pads[p], &pad);
			if (copy_to_user(&links->pads[p], &pad, sizeof(pad)))
				return -EFAULT;
		}
	}

	if (links->links) {
		struct media_link_desc __user *ulink;
		unsigned int l;

		for (l = 0, ulink = links->links; l < entity->num_links; l++) {
			struct media_link_desc link;

			/* Ignore backlinks. */
			if (entity->links[l].source->entity != entity)
				continue;

			memset(&link, 0, sizeof(link));
			media_device_kpad_to_upad(entity->links[l].source,
						  &link.source);
			media_device_kpad_to_upad(entity->links[l].sink,
						  &link.sink);
			link.flags = entity->links[l].flags;
			if (copy_to_user(ulink, &link, sizeof(*ulink)))
				return -EFAULT;
			ulink++;
		}
	}

	return 0;
}

static long media_device_enum_links(struct media_device *mdev,
				    struct media_links_enum __user *ulinks)
{
	struct media_links_enum links;
	int rval;

	if (copy_from_user(&links, ulinks, sizeof(links)))
		return -EFAULT;

	rval = __media_device_enum_links(mdev, &links);
	if (rval < 0)
		return rval;

	if (copy_to_user(ulinks, &links, sizeof(*ulinks)))
		return -EFAULT;

	return 0;
}

static long media_device_setup_link(struct media_device *mdev,
				    struct media_link_desc __user *_ulink)
{
	struct media_link *link = NULL;
	struct media_link_desc ulink;
	struct media_entity *source;
	struct media_entity *sink;
	int ret;

	if (copy_from_user(&ulink, _ulink, sizeof(ulink)))
		return -EFAULT;

	/* Find the source and sink entities and link.
	 */
	source = find_entity(mdev, ulink.source.entity);
	sink = find_entity(mdev, ulink.sink.entity);

	if (source == NULL || sink == NULL)
		return -EINVAL;

	if (ulink.source.index >= source->num_pads ||
	    ulink.sink.index >= sink->num_pads)
		return -EINVAL;

	link = media_entity_find_link(&source->pads[ulink.source.index],
				      &sink->pads[ulink.sink.index]);
	if (link == NULL)
		return -EINVAL;

	/* Setup the link on both entities. */
	ret = __media_entity_setup_link(link, ulink.flags);

	if (copy_to_user(_ulink, &ulink, sizeof(ulink)))
		return -EFAULT;

	return ret;
}

static long media_device_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct media_devnode *devnode = media_devnode_data(filp);
	struct media_device *dev = devnode->media_dev;
	long ret;

	switch (cmd) {
	case MEDIA_IOC_DEVICE_INFO:
		ret = media_device_get_info(dev,
				(struct media_device_info __user *)arg);
		break;

	case MEDIA_IOC_ENUM_ENTITIES:
		ret = media_device_enum_entities(dev,
				(struct media_entity_desc __user *)arg);
		break;

	case MEDIA_IOC_ENUM_LINKS:
		mutex_lock(&dev->graph_mutex);
		ret = media_device_enum_links(dev,
				(struct media_links_enum __user *)arg);
		mutex_unlock(&dev->graph_mutex);
		break;

	case MEDIA_IOC_SETUP_LINK:
		mutex_lock(&dev->graph_mutex);
		ret = media_device_setup_link(dev,
				(struct media_link_desc __user *)arg);
		mutex_unlock(&dev->graph_mutex);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

#ifdef CONFIG_COMPAT

struct media_links_enum32 {
	__u32 entity;
	compat_uptr_t pads; /* struct media_pad_desc * */
	compat_uptr_t links; /* struct media_link_desc * */
	__u32 reserved[4];
};

static long media_device_enum_links32(struct media_device *mdev,
				      struct media_links_enum32 __user *ulinks)
{
	struct media_links_enum links;
	compat_uptr_t pads_ptr, links_ptr;

	memset(&links, 0, sizeof(links));

	if (get_user(links.entity, &ulinks->entity)
	    || get_user(pads_ptr, &ulinks->pads)
	    || get_user(links_ptr, &ulinks->links))
		return -EFAULT;

	links.pads = compat_ptr(pads_ptr);
	links.links = compat_ptr(links_ptr);

	return __media_device_enum_links(mdev, &links);
}

#define MEDIA_IOC_ENUM_LINKS32		_IOWR('|', 0x02, struct media_links_enum32)

static long media_device_compat_ioctl(struct file *filp, unsigned int cmd,
				      unsigned long arg)
{
	struct media_devnode *devnode = media_devnode_data(filp);
	struct media_device *dev = devnode->media_dev;
	long ret;

	switch (cmd) {
	case MEDIA_IOC_DEVICE_INFO:
	case MEDIA_IOC_ENUM_ENTITIES:
	case MEDIA_IOC_SETUP_LINK:
		return media_device_ioctl(filp, cmd, arg);

	case MEDIA_IOC_ENUM_LINKS32:
		mutex_lock(&dev->graph_mutex);
		ret = media_device_enum_links32(dev,
				(struct media_links_enum32 __user *)arg);
		mutex_unlock(&dev->graph_mutex);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}
#endif /* CONFIG_COMPAT */

static const struct media_file_operations media_device_fops = {
	.owner = THIS_MODULE,
	.open = media_device_open,
	.ioctl = media_device_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = media_device_compat_ioctl,
#endif /* CONFIG_COMPAT */
	.release = media_device_close,
};

/* -----------------------------------------------------------------------------
 * sysfs
 */

static ssize_t show_model(struct device *cd,
			  struct device_attribute *attr, char *buf)
{
	struct media_devnode *devnode = to_media_devnode(cd);
	struct media_device *mdev = devnode->media_dev;

	return sprintf(buf, "%.*s\n", (int)sizeof(mdev->model), mdev->model);
}

static DEVICE_ATTR(model, S_IRUGO, show_model, NULL);

/* -----------------------------------------------------------------------------
 * Registration/unregistration
 */

static void media_device_release(struct media_devnode *mdev)
{
}

/**
 * media_device_register - register a media device
 * @mdev:	The media device
 *
 * The caller is responsible for initializing the media device before
 * registration. The following fields must be set:
 *
 * - dev must point to the parent device
 * - model must be filled with the device model name
 */
int __must_check __media_device_register(struct media_device *mdev,
					 struct module *owner)
{
	struct media_devnode *devnode;
	int ret;

	if (WARN_ON(mdev->dev == NULL || mdev->model[0] == 0))
		return -EINVAL;

	mdev->entity_id = 1;
	INIT_LIST_HEAD(&mdev->entities);
	spin_lock_init(&mdev->lock);
	mutex_init(&mdev->graph_mutex);

	devnode = kzalloc(sizeof(*devnode), GFP_KERNEL);
	if (!devnode)
		return -ENOMEM;

	/* Register the device node. */
	mdev->devnode = devnode;
	devnode->fops = &media_device_fops;
	devnode->parent = mdev->dev;
	devnode->release = media_device_release;
	ret = media_devnode_register(mdev, devnode, owner);
	if (ret < 0) {
		/* devnode free is handled in media_devnode_*() */
		mdev->devnode = NULL;
		return ret;
	}

	ret = device_create_file(&devnode->dev, &dev_attr_model);
	if (ret < 0) {
		/* devnode free is handled in media_devnode_*() */
		mdev->devnode = NULL;
		media_devnode_unregister_prepare(devnode);
		media_devnode_unregister(devnode);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__media_device_register);

/**
 * media_device_unregister - unregister a media device
 * @mdev:	The media device
 *
 */
void media_device_unregister(struct media_device *mdev)
{
	struct media_entity *entity;
	struct media_entity *next;

	/* Clear the devnode register bit to avoid races with media dev open */
	media_devnode_unregister_prepare(mdev->devnode);

	list_for_each_entry_safe(entity, next, &mdev->entities, list)
		media_device_unregister_entity(entity);

	device_remove_file(&mdev->devnode->dev, &dev_attr_model);
	media_devnode_unregister(mdev->devnode);
	/* devnode free is handled in media_devnode_*() */
	mdev->devnode = NULL;
}
EXPORT_SYMBOL_GPL(media_device_unregister);

/**
 * media_device_register_entity - Register an entity with a media device
 * @mdev:	The media device
 * @entity:	The entity
 */
int __must_check media_device_register_entity(struct media_device *mdev,
					      struct media_entity *entity)
{
	/* Warn if we apparently re-register an entity */
	WARN_ON(entity->parent != NULL);
	entity->parent = mdev;

	spin_lock(&mdev->lock);
	if (entity->id == 0)
		entity->id = mdev->entity_id++;
	else
		mdev->entity_id = max(entity->id + 1, mdev->entity_id);
	list_add_tail(&entity->list, &mdev->entities);
	spin_unlock(&mdev->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(media_device_register_entity);

/**
 * media_device_unregister_entity - Unregister an entity
 * @entity:	The entity
 *
 * If the entity has never been registered this function will return
 * immediately.
 */
void media_device_unregister_entity(struct media_entity *entity)
{
	struct media_device *mdev = entity->parent;

	if (mdev == NULL)
		return;

	spin_lock(&mdev->lock);
	list_del(&entity->list);
	spin_unlock(&mdev->lock);
	entity->parent = NULL;
}
EXPORT_SYMBOL_GPL(media_device_unregister_entity);

/**
 * media_device_init() - initialize a media device
 * @mdev:	The media device
 *
 * The caller is responsible for initializing the media device before
 * registration. The following fields must be set:
 *
 * - dev must point to the parent device
 * - model must be filled with the device model name
 */
void media_device_init(struct media_device *mdev)
{
	INIT_LIST_HEAD(&mdev->entities);
	INIT_LIST_HEAD(&mdev->interfaces);
	INIT_LIST_HEAD(&mdev->pads);
	INIT_LIST_HEAD(&mdev->links);
	INIT_LIST_HEAD(&mdev->entity_notify);
	spin_lock_init(&mdev->lock);
	mutex_init(&mdev->graph_mutex);
	ida_init(&mdev->entity_internal_idx);

	dev_dbg(mdev->dev, "Media device initialized\n");
}
EXPORT_SYMBOL_GPL(media_device_init);

static void media_device_release_devres(struct device *dev, void *res)
{
}

struct media_device *media_device_get_devres(struct device *dev)
{
	struct media_device *mdev;

	mdev = devres_find(dev, media_device_release_devres, NULL, NULL);
	if (mdev)
		return mdev;

	mdev = devres_alloc(media_device_release_devres,
				sizeof(struct media_device), GFP_KERNEL);
	if (!mdev)
		return NULL;
	return devres_get(dev, mdev, NULL, NULL);
}
EXPORT_SYMBOL_GPL(media_device_get_devres);

struct media_device *media_device_find_devres(struct device *dev)
{
	return devres_find(dev, media_device_release_devres, NULL, NULL);
}
EXPORT_SYMBOL_GPL(media_device_find_devres);

#if IS_ENABLED(CONFIG_PCI)
void media_device_pci_init(struct media_device *mdev,
			   struct pci_dev *pci_dev,
			   const char *name)
{
	mdev->dev = &pci_dev->dev;

	if (name)
		strlcpy(mdev->model, name, sizeof(mdev->model));
	else
		strlcpy(mdev->model, pci_name(pci_dev), sizeof(mdev->model));

	sprintf(mdev->bus_info, "PCI:%s", pci_name(pci_dev));

	mdev->hw_revision = (pci_dev->subsystem_vendor << 16)
			    | pci_dev->subsystem_device;

	mdev->driver_version = LINUX_VERSION_CODE;

	media_device_init(mdev);
}
EXPORT_SYMBOL_GPL(media_device_pci_init);
#endif

#if IS_ENABLED(CONFIG_USB)
void __media_device_usb_init(struct media_device *mdev,
			     struct usb_device *udev,
			     const char *board_name,
			     const char *driver_name)
{
	mdev->dev = &udev->dev;

	if (driver_name)
		strlcpy(mdev->driver_name, driver_name,
			sizeof(mdev->driver_name));

	if (board_name)
		strlcpy(mdev->model, board_name, sizeof(mdev->model));
	else if (udev->product)
		strlcpy(mdev->model, udev->product, sizeof(mdev->model));
	else
		strlcpy(mdev->model, "unknown model", sizeof(mdev->model));
	if (udev->serial)
		strlcpy(mdev->serial, udev->serial, sizeof(mdev->serial));
	usb_make_path(udev, mdev->bus_info, sizeof(mdev->bus_info));
	mdev->hw_revision = le16_to_cpu(udev->descriptor.bcdDevice);
	mdev->driver_version = LINUX_VERSION_CODE;

	media_device_init(mdev);
}
EXPORT_SYMBOL_GPL(__media_device_usb_init);
#endif
