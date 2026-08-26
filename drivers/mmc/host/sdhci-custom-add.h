#ifndef __SDHCI_CUSTOM_ADD_H__
#define __SDHCI_CUSTOM_ADD_H__

#include <linux/pm_runtime.h>

static inline int sdhci_custom_runtime_pm_get(struct sdhci_host *host)
{
	return pm_runtime_get_sync(host->mmc->parent);
}

static inline int sdhci_custom_runtime_pm_put(struct sdhci_host *host)
{
	pm_runtime_mark_last_busy(host->mmc->parent);
	return pm_runtime_put_autosuspend(host->mmc->parent);
}

#endif /* __SDHCI_CUSTOM_ADD_H__ */
