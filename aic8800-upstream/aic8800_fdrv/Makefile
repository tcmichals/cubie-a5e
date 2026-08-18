# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_AIC8800_WLAN_SUPPORT) += aic8800_fdrv.o

aic8800_fdrv-y := \
	rwnx_msg_tx.o          \
	rwnx_msg_rx.o          \
	rwnx_utils.o           \
	rwnx_cmds.o            \
	rwnx_irqs.o            \
	rwnx_cfgfile.o         \
	rwnx_strs.o            \
	rwnx_rx.o              \
	rwnx_tx.o              \
	rwnx_txq.o             \
	rwnx_main.o            \
	rwnx_mod_params.o      \
	rwnx_mesh.o            \
	rwnx_platform.o        \
	aicwf_chip_8801.o          \
	aicwf_chip_8800dc.o          \
	aicwf_chip_8800d80.o          \
	aicwf_chip_ops.o            \
	rwnx_v7.o              \
	ipc_host.o             \
	rwnx_tdls.o            \
	aic_vendor.o	       \
	md5.o                  \
	aicwf_compat_8800dc.o  \
	aicwf_compat_8800d80.o \
	rwnx_wakelock.o		\
	regdb.o				   \
	aic_priv_cmd.o		\
	aicwf_rx_prealloc.o	\
	aicwf_txrxif.o

aic8800_fdrv-$(CONFIG_AIC8800_GENL)             		+= aicwf_genl.o
aic8800_fdrv-$(CONFIG_AIC8800_BR_SUPPORT)       		+= aic_br_ext.o
aic8800_fdrv-$(CONFIG_RWNX_RADAR)       		+= rwnx_radar.o
aic8800_fdrv-$(CONFIG_RWNX_DEBUGFS)         	+= rwnx_debugfs.o
aic8800_fdrv-$(CONFIG_RWNX_DEBUGFS)         	+= rwnx_fw_trace.o
aic8800_fdrv-$(CONFIG_NL80211_TESTMODE) 		+= rwnx_testmode.o
aic8800_fdrv-$(CONFIG_RWNX_BFMER)       		+= rwnx_bfmer.o
aic8800_fdrv-$(CONFIG_RWNX_MUMIMO_TX)   		+= rwnx_mu_group.o
aic8800_fdrv-$(CONFIG_AIC8800_SDIO_SUPPORT)     		+= sdio_host.o
aic8800_fdrv-$(CONFIG_AIC8800_SDIO_SUPPORT)     		+= aicwf_sdio.o
aic8800_fdrv-$(CONFIG_AIC8800_USB_SUPPORT)      		+= usb_host.o
aic8800_fdrv-$(CONFIG_AIC8800_USB_SUPPORT)      		+= aicwf_usb.o
aic8800_fdrv-$(CONFIG_AIC8800_FILTER_TCP_ACK)   		+= aicwf_tcp_ack.o
aic8800_fdrv-$(CONFIG_AIC8800_SDIO_BT)   			+= aic_btsdio.o
aic8800_fdrv-$(CONFIG_AIC8800_GKI)     				+= rwnx_gki.o
aic8800_fdrv-$(CONFIG_AIC8800_USE_WIRELESS_EXT)  	+= aicwf_wext_linux.o
aic8800_fdrv-$(CONFIG_AIC8800_CREATE_TRACE_POINTS) 	+= rwnx_trace.o

ccflags-y += -I$(srctree)
ccflags-y += -I$(src)
ccflags-y += -I$(src)/../aic8800_bsp
ccflags-y += -DAIC_TRACE_INCLUDE_PATH=$(src)

ccflags-$(CONFIG_AIC8800_USB_SUPPORT) 					+= -DAICWF_USB_SUPPORT

ccflags-$(CONFIG_RWNX_DEBUGFS) 					+= -DCONFIG_RWNX_DEBUGFS
ccflags-$(CONFIG_RWNX_P2P_DEBUGFS) 				+= -DCONFIG_RWNX_P2P_DEBUGFS
ccflags-$(CONFIG_RWNX_TL4) 						+= -DCONFIG_RWNX_TL4
ccflags-$(CONFIG_RWNX_OLD_IPC) 					+= -DCONFIG_RWNX_OLD_IPC
ccflags-$(CONFIG_AIC8800_BSP_PLATFORM_EXTERNAL) 			+= -DCONFIG_PLATFORM_EXTERNAL
ccflags-$(CONFIG_AIC8800_VRF_DCDC_MODE) 				+= -DCONFIG_VRF_DCDC_MODE
ccflags-$(CONFIG_AIC8800_HE_FOR_OLD_KERNEL) 			+= -DCONFIG_HE_FOR_OLD_KERNEL
ccflags-$(CONFIG_AIC8800_COEX) 							+= -DCONFIG_COEX
ccflags-$(CONFIG_RWNX_FULLMAC) 					+= -DCONFIG_RWNX_FULLMAC
ccflags-$(CONFIG_RWNX_RADAR) 					+= -DCONFIG_RWNX_RADAR
ccflags-$(CONFIG_RWNX_MON_DATA) 				+= -DCONFIG_RWNX_MON_DATA
ccflags-$(CONFIG_RWNX_BFMER) 					+= -DCONFIG_RWNX_BFMER
ccflags-$(CONFIG_RWNX_SPLIT_TX_BUF) 			+= -DCONFIG_RWNX_SPLIT_TX_BUF
ccflags-$(CONFIG_RWNX_AMSDUS_TX) 				+= -DCONFIG_RWNX_AMSDUS_TX
ccflags-$(CONFIG_RWNX_DBG) 						+= -DCONFIG_RWNX_DBG
ccflags-$(CONFIG_RWNX_SW_PROFILING) 			+= -DCONFIG_RWNX_SW_PROFILING
ccflags-$(CONFIG_RWNX_MUMIMO_TX) 				+= -DCONFIG_RWNX_MUMIMO_TX
ccflags-$(CONFIG_AIC8800_USE_5G) 						+= -DUSE_5G
ccflags-$(CONFIG_AIC8800_CREATE_TRACE_POINTS) 			+= -DAIC8800_CREATE_TRACE_POINTS
ccflags-$(CONFIG_AIC8800_TXRX_THREAD_PRIO) 				+= -DCONFIG_TXRX_THREAD_PRIO
ccflags-$(CONFIG_AIC8800_GPIO_WAKEUP) 					+= -DCONFIG_GPIO_WAKEUP
ccflags-$(CONFIG_AIC8800_SET_VENDOR_EXTENSION_IE) 		+= -DCONFIG_SET_VENDOR_EXTENSION_IE
ccflags-$(CONFIG_AIC8800_SUPPORT_REALTIME_CHANGE_MAC) 	+= -DCONFIG_SUPPORT_REALTIME_CHANGE_MAC
ccflags-$(CONFIG_AIC8800_WPA3_FOR_OLD_KERNEL) 			+= -DCONFIG_WPA3_FOR_OLD_KERNEL
ccflags-$(CONFIG_AIC8800_VHT_FOR_OLD_KERNEL) 			+= -DCONFIG_VHT_FOR_OLD_KERNEL
ccflags-$(CONFIG_AIC8800_PREALLOC_RX_SKB) 				+= -DCONFIG_PREALLOC_RX_SKB
ccflags-$(CONFIG_AIC8800_WIFI_SUSPEND_FOR_LINUX) 		+= -DCONFIG_WIFI_SUSPEND_FOR_LINUX
ccflags-$(CONFIG_AIC8800_USE_P2P0) 						+= -DCONFIG_USE_P2P0
ccflags-$(CONFIG_AIC8800_FDRV_NO_REG_SDIO) 				+= -DCONFIG_FDRV_NO_REG_SDIO
ccflags-$(CONFIG_AIC8800_SCHED_SCAN) 					+= -DCONFIG_SCHED_SCAN
ccflags-$(CONFIG_AIC8800_OOB) 							+= -DCONFIG_OOB
ccflags-$(CONFIG_AIC8800_USE_CUSTOMER_MAC) 				+= -DCONFIG_USE_CUSTOMER_MAC
ccflags-$(CONFIG_AIC8800_PREALLOC_TXQ) 					+= -DCONFIG_PREALLOC_TXQ
ccflags-$(CONFIG_AIC8800_DPD) 							+= -DCONFIG_DPD
ccflags-$(CONFIG_AIC8800_FORCE_DPD_CALIB) 			+= -DCONFIG_FORCE_DPD_CALIB
ccflags-$(CONFIG_AIC8800_FILTER_TCP_ACK) 				+= -DCONFIG_FILTER_TCP_ACK
ccflags-$(CONFIG_AIC8800_SDIO_BT) 						+= -DCONFIG_SDIO_BT
ccflags-$(CONFIG_AIC8800_RESV_MEM_SUPPORT) 				+= -DCONFIG_RESV_MEM_SUPPORT
ccflags-$(CONFIG_AIC8800_GKI) 							+= -DCONFIG_GKI
ccflags-$(CONFIG_AIC8800_TEMP_COMP) 					+= -DCONFIG_TEMP_COMP
ccflags-$(CONFIG_AIC8800_AUTO_CUSTREG) 					+= -DCONFIG_AUTO_CUSTREG
ccflags-$(CONFIG_AIC8800_TEMP_CONTROL) 					+= -DCONFIG_TEMP_CONTROL
ccflags-$(CONFIG_AIC8800_WOWLAN_PM) 					+= -DCONFIG_WOWLAN_PM
ccflags-$(CONFIG_AIC8800_GENL) 							+= -DCONFIG_GENL
ccflags-$(CONFIG_AIC8800_POWER_LIMIT) 					+= -DCONFIG_POWER_LIMIT
ccflags-$(CONFIG_AIC8800_REGION_PW) 					+= -DCONFIG_REGION_PW
ccflags-$(CONFIG_AIC8800_MCC) 							+= -DCONFIG_MCC
ccflags-$(CONFIG_AICWF_LATENCY_MODE) 			+= -DAICWF_LATENCY_MODE
ccflags-$(CONFIG_AIC8800_USE_WIRELESS_EXT) 				+= -DCONFIG_USE_WIRELESS_EXT
ccflags-$(CONFIG_AIC8800_AUTO_POWERSAVE) 				+= -DCONFIG_AUTO_POWERSAVE
ccflags-$(CONFIG_AIC8800_NO_WAKE_COUNT) 				+= -DCONFIG_NO_WAKE_COUNT
ccflags-$(CONFIG_AIC8800_SDIO_SUPPORT) 					+= -DAICWF_SDIO_SUPPORT
ccflags-$(CONFIG_AIC8800_SDIO_PWRCTRL) 					+= -DCONFIG_SDIO_PWRCTRL
ccflags-$(CONFIG_AIC8800_BR_SUPPORT) 					+= -DCONFIG_BR_SUPPORT
ccflags-$(CONFIG_AIC8800_RX_REORDER) 					+= -DAICWF_RX_REORDER
ccflags-$(CONFIG_AIC8800_ARP_OFFLOAD) 					+= -DAICWF_ARP_OFFLOAD
ccflags-$(CONFIG_AIC8800_RADAR_OR_IR_DETECT) 				+= -DRADAR_OR_IR_DETECT
ccflags-$(CONFIG_AIC8800_RX_NETIF_RECV_SKB) 			+= -DCONFIG_RX_NETIF_RECV_SKB
ccflags-$(CONFIG_AIC8800_ONE_TXQ)  						+= -DCONFIG_ONE_TXQ
ccflags-$(CONFIG_AIC8800_TX_NETIF_FLOWCTRL) 			+= -DCONFIG_TX_NETIF_FLOWCTRL
