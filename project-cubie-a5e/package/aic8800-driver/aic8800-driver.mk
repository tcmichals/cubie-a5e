################################################################################
#
# aic8800-driver (upstream mainline, RFC v2 wireless-next)
#
# Two-module architecture:
#   aic8800_bsp.ko  - SDIO bus, chip init, firmware upload, start_app
#   aic8800_fdrv.ko - cfg80211 FullMAC WLAN driver
#
################################################################################

AIC8800_DRIVER_SITE = $(BR2_EXTERNAL_CUBIE_A5E_PATH)/../aic8800-upstream
AIC8800_DRIVER_SITE_METHOD = local
AIC8800_DRIVER_LICENSE = GPL-2.0

# Build from the top-level directory (contains aic8800_bsp/ and aic8800_fdrv/)
# No MODULE_SUBDIRS needed — the top-level Makefile dispatches to both.

AIC8800_DRIVER_MODULE_MAKE_OPTS = \
	KDIR=$(LINUX_DIR) \
	KSRC=$(LINUX_DIR) \
	ARCH=arm64 \
	CROSS_COMPILE=$(TARGET_CROSS) \
	CONFIG_AIC8800_BSP_SUPPORT=m \
	CONFIG_AIC8800_WLAN_SUPPORT=m \
	CONFIG_AIC8800_SDIO_SUPPORT=y \
	CONFIG_AIC8800_SDIO_PWRCTRL=y \
	CONFIG_AIC8800_VRF_DCDC_MODE=y \
	CONFIG_AIC8800_PREALLOC_TXQ=y \
	CONFIG_AIC8800_DPD=y \
	CONFIG_AIC8800_FORCE_DPD_CALIB=y \
	CONFIG_AIC8800_RESV_MEM_SUPPORT=y \
	CONFIG_RWNX_FULLMAC=y \
	CONFIG_RWNX_RADAR=y \
	CONFIG_RWNX_BCMC=y \
	CONFIG_RWNX_MON_DATA=y \
	CONFIG_RWNX_DBG=y \
	CONFIG_AIC8800_USE_5G=y \
	CONFIG_AIC8800_COEX=y \
	CONFIG_AIC8800_RX_REORDER=y \
	CONFIG_AIC8800_ARP_OFFLOAD=y \
	CONFIG_AIC8800_RX_NETIF_RECV_SKB=y \
	CONFIG_AIC8800_TXRX_THREAD_PRIO=y \
	CONFIG_AIC8800_FILTER_TCP_ACK=n \
	CONFIG_AIC8800_SUPPORT_REALTIME_CHANGE_MAC=y \
	CONFIG_AIC8800_AUTO_POWERSAVE=y \
	CONFIG_AIC8800_WOWLAN_PM=y \
	CONFIG_AIC8800_POWER_LIMIT=y \
	CONFIG_AIC8800_REGION_PW=y \
	CONFIG_AIC8800_MCC=y \
	CONFIG_AIC8800_TEMP_COMP=y \
	CONFIG_AIC8800_AUTO_CUSTREG=y \
	CONFIG_AIC8800_TEMP_CONTROL=y \
	CONFIG_AIC8800_PREALLOC_RX_SKB=y \
	CONFIG_AIC8800_FDRV_NO_REG_SDIO=y

# Features we explicitly DISABLE for Cubie A5E:
#   GPIO_WAKEUP=n  — we use in-band SDIO DAT1 interrupts
#   OOB=n          — no out-of-band GPIO interrupt
#   SDIO_BT=n      — depends on BROKEN in upstream

$(eval $(kernel-module))
$(eval $(generic-package))
