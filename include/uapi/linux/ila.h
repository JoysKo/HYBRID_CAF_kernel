/* ila.h - ILA Interface */

#ifndef _UAPI_LINUX_ILA_H
#define _UAPI_LINUX_ILA_H

enum {
	ILA_ATTR_UNSPEC,
	ILA_ATTR_LOCATOR,			/* u64 */
	ILA_ATTR_IDENTIFIER,			/* u64 */
	ILA_ATTR_LOCATOR_MATCH,			/* u64 */
	ILA_ATTR_IFINDEX,			/* s32 */
	ILA_ATTR_DIR,				/* u32 */
	ILA_ATTR_PAD,

	__ILA_ATTR_MAX,
};

#define ILA_ATTR_MAX		(__ILA_ATTR_MAX - 1)

#endif /* _UAPI_LINUX_ILA_H */
