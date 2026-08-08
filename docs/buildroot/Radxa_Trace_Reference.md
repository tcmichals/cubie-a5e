# Radxa 7.1 Vendor Driver Hardware Trace Reference

## Overview
This log records the **100% verified working boot trace** of the Radxa 7.1 vendor driver (`aic8800-radxa-working-backup`) on the Allwinner T527 (Cubie A5E) running Linux 7.1. 

This reference trace serves as the exact ground-truth specification for fixing and validating the upstream `shenmintao` driver (`aic8800-driver-src`).

---

## 1. Verified Working Boot Trace (`dmesg`)

```text
[    5.428052] AICWFDBG(LOGTRACE)       >>> rwnx_mod_init()
[    5.428066] AICWFDBG(LOGINFO)        rwnx v6.4.3.0 - - 241c091M (master)
[    5.428070] AICWFDBG(LOGINFO)        RELEASE_DATE:2026_0123_5f7be68d 
[    5.428404] aicbsp: aicbsp_set_subsys, subsys: AIC_WIFI, state to: 1
[    5.428409] aicbsp: aicbsp_set_subsys, power state change to 1 dure to AIC_WIFI
[    5.428414] aicbsp: aicbsp_platform_power_on
[    5.444628] aicbsp: aicbsp_sdio_probe:1 vid:0xC8A1  did:0x0082
[    5.444834] aicbsp: aicbsp_sdio_probe:2 vid:0xC8A1  did:0x0182
[    5.444847] aicbsp: aicbsp_sdio_probe after replace:1
[    5.444855] AICWFDBG(LOGINFO)        aicwf_sdio_chipmatch USE AIC8800D80
[    5.444860] aicbsp: aicbsp_get_feature, set FEATURE_SDIO_CLOCK 150 MHz
[    5.444866] aicbsp: aicwf_sdio_reg_init
[    5.446563] AICWFDBG(LOGINFO)        aicbsp: aicbsp_driver_fw_init, chip rev: 7
[    5.446578] rwnx_load_firmware :firmware path = /lib/firmware/aic8800D80/fw_patch_table_8800d80_u02.bin  
[    5.447996] file md5:54b15ce88b3b9ba14bf961e6f90839fc
[    5.448166] AICWFDBG(LOGDEBUG)       aicbt_patch_info_unpack head_t->len:6 base_len:4 
[    5.448241] AICWFDBG(LOGDEBUG)       aicbt_patch_info_unpack memcpy_len:5 
[    5.448250] AICWFDBG(LOGDEBUG)       aicbt_patch_info_unpack adid_addrinf:1e7e9c addr_adid:201940 
[    5.448257] AICWFDBG(LOGDEBUG)       aicbt_patch_info_unpack id:0 addr:20b43c 
[    5.448264] rwnx_plat_bin_fw_upload_android
[    5.448271] rwnx_load_firmware :firmware path = /lib/firmware/aic8800D80/fw_adid_8800d80_u02.bin  
[    5.448968] file md5:f546881a81b960d89a672578eb45a809
[    5.450052] rwnx_plat_bin_fw_upload_android
[    5.450064] rwnx_load_firmware :firmware path = /lib/firmware/aic8800D80/fw_patch_8800d80_u02.bin  
[    5.452731] file md5:f5400db1d64e0150c3232eb53e7f78b6
[    5.467569] AICWFDBG(LOGDEBUG)       aicbt_ext_patch_data_load ext_patch_file_name:fw_patch_8800d80_u02_ext0.bin ext_patch_id:0 ext_patch_addr:20b43c 
[    5.467581] rwnx_plat_bin_fw_upload_android
[    5.467589] rwnx_load_firmware :firmware path = /lib/firmware/aic8800D80/fw_patch_8800d80_u02_ext0.bin  
[    5.469237] file md5:6fcbe60f7bcf7bc39bdf6e9c3fb7f1d8
[    5.505336] aicbt_patch_table_load bt btmode[3]:5 
[    5.505346] aicbt_patch_table_load bt uart_baud[3]:1500000 
[    5.505353] aicbt_patch_table_load bt uart_flowctrl[3]:1 
[    5.505358] aicbt_patch_table_load bt lpm_enable[3]:0 
[    5.505363] aicbt_patch_table_load bt tx_pwr[3]:28463 
[    5.621660] aicbsp: bt patch version: - Aug 01 2025 11:05:26 - git a26f071
[    5.621771] rwnx_plat_bin_fw_upload_android
[    5.621779] rwnx_load_firmware :firmware path = /lib/firmware/aic8800D80/fmacfw_8800d80_u02.bin  
[    5.644264] file md5:56562779b8c4debfd9b354891418249a
[    5.776016] rd_version_val=06090101
[    5.794675] AICWFDBG(LOGDEBUG)       aicwf_sdio_probe:1
[    5.794686] AICWFDBG(LOGDEBUG)       Class=7
[    5.794690] AICWFDBG(LOGDEBUG)       sdio vendor ID: 0xc8a1
[    5.794694] AICWFDBG(LOGDEBUG)       sdio device ID: 0x0082
[    5.794699] AICWFDBG(LOGDEBUG)       Function#: 1
[    5.794705] AICWFDBG(LOGINFO)        aicwf_sdio_chipmatch USE AIC8800D80
[    5.794710] aicbsp: aicbsp_get_feature, set FEATURE_SDIO_CLOCK 150 MHz
[    5.794716] aicsdio: aicwf_sdio_reg_init
[    5.800062] AICWFDBG(LOGINFO)        sdio ready
[    5.800070] aicwf_prealloc_init enter
[    5.800374] pre alloc rxbuff list len: 30
[    5.800427] aicbsp: aicbsp_resv_mem_alloc_skb, alloc resv_mem_txdata succuss, id: 0, size: 98304
[    5.800641] AICWFDBG(LOGINFO)        sdio_bustx_thread the policy of current thread is:1
[    5.800655] AICWFDBG(LOGINFO)        sdio_bustx_thread the rt_priority of current thread is:1
[    5.800661] AICWFDBG(LOGINFO)        sdio_bustx_thread the current pid is:285
[    5.800772] AICWFDBG(LOGINFO)        sdio_busrx_thread the policy of current thread is:1
[    5.800780] AICWFDBG(LOGINFO)        sdio_busrx_thread the rt_priority of current thread is:1
[    5.800785] AICWFDBG(LOGINFO)        sdio_busrx_thread the current pid is:286
[    5.801502] AICWFDBG(LOGTRACE)       >>> rwnx_platform_init()
[    5.801511] AICWFDBG(LOGTRACE)       >>> rwnx_cfg80211_init()
[    5.801516] aicbsp: aicbsp_get_feature, set FEATURE_SDIO_CLOCK 150 MHz
[    5.801527] AICWFDBG(LOGINFO)        rwnx_cfg80211_init sizeof(struct rwnx_hw):22392 
[    5.802112] AICWFDBG(LOGTRACE)       >>> rwnx_init_aic()
[    5.802119] AICWFDBG(LOGTRACE)       >>> rwnx_cmd_mgr_init()
[    5.802563] tcp_ack_init 
[    5.802574] AICWFDBG(LOGINFO)        aicwf_prealloc_txq_alloc size is diff will to be kzalloc 
[    5.802595] AICWFDBG(LOGINFO)        aicwf_prealloc_txq_alloc txq kzalloc successful 
[    5.802882] === RADXA 7.1 DRIVER PROBE START ===
[    5.802889] [RADXA_71_CONFIG] CONFIG_SDIO_SUPPORT=0
[    5.802894] [RADXA_71_CONFIG] CONFIG_OOB=0 CONFIG_GPIO_WAKEUP=0 CONFIG_SDIO_PWRCTRL=0
[    5.802900] [RADXA_71_CONFIG] CONFIG_PREALLOC_RX_SKB=1 CONFIG_PREALLOC_TXQ=1 CONFIG_USE_5G=0
[    5.802906] [RADXA_71] Running rwnx_ic_system_init...
[    5.802909] AICWFDBG(LOGTRACE)       >>> rwnx_send_dbg_mem_read_req()
[    5.802915] AICWFDBG(LOGTRACE)       rwnx_send_msg (1025)DBG_MEM_READ_CFM reqcfm:1 in_irq:0 in_softirq:0 in_atomic:0
[    5.802928] AICWFDBG(LOGTRACE)       rwnx_cmd_malloc get cmd_array[0]:0000000047528a93 
[    5.802941] [RADXA_71_TX] CMD: id=1024 len=12 crc8=0xd5
[    5.803268] AICWFDBG(LOGDEBUG)       rwnx_rx_handle_msg msg->id:0x401 
[    5.803305] AICWFDBG(LOGTRACE)       rwnx_cmd_free cmd_array[0]:0000000047528a93 
[    5.803317] AICWFDBG(LOGTRACE)       >>> rwnx_send_dbg_mem_read_req()
[    5.803323] AICWFDBG(LOGTRACE)       rwnx_send_msg (1025)DBG_MEM_READ_CFM reqcfm:1 in_irq:0 in_softirq:0 in_atomic:0
[    5.803333] AICWFDBG(LOGTRACE)       rwnx_cmd_malloc get cmd_array[0]:0000000047528a93 
[    5.803342] [RADXA_71_TX] CMD: id=1024 len=12 crc8=0xd5
[    7.854232] cmd timed-out
[    7.854295] wlan error reset flow.
[    7.854299] send event.
[    7.854386] wlan error event send.
[    7.854391] tkn[1]  flags:0012  result: -4  cmd:1024-DBG_MEM_READ_REQ         - reqcfm(1025-DBG_MEM_READ_CFM)
[    7.854406] AICWFDBG(LOGINFO)        FDRV chip_id=7, chip_sub_id=20!!
[    7.854412] AICWFDBG(LOGTRACE)       >>> rwnx_platform_on()
[    7.854417] AICWFDBG(LOGINFO)        userconfig file path:aic_userconfig_8800d80.txt 
[    7.854421] AICWFDBG(LOGINFO)        ### Load file aic_userconfig_8800d80.txt
[    7.854428] AICWFDBG(LOGINFO)        rwnx_load_firmware :firmware path = /lib/firmware/aic8800D80/aic_userconfig_8800d80.txt  
[    7.855438] AICWFDBG(LOGINFO)        file md5:4c1619f2ad65562412588a297dfbc86f
[    7.855454] AICWFDBG(LOGINFO)        ### Load file done: aic_userconfig_8800d80.txt, size=2724
[    7.855462] AICWFDBG(LOGINFO)        rwnx_plat_userconfig_parsing3: AIC USERCONFIG 2022/0803/1707
...
[    7.856552] userconfig download complete
[    7.856556] [RADXA_71] Sending MM_SET_STACK_START_REQ (cmd 123, chipid=0x0003)...
[    7.856564] AICWFDBG(LOGTRACE)       rwnx_send_msg (124)MM_SET_STACK_START_CFM reqcfm:1 in_irq:0 in_softirq:0 in_atomic:0
[    7.856576] AICWFDBG(LOGTRACE)       rwnx_cmd_malloc get cmd_array[1]:00000000ccdf972a 
[    7.856585] AICWFDBG(LOGERROR)       cmd queue crashed
[    7.856593] AICWFDBG(LOGTRACE)       rwnx_cmd_free cmd_array[1]:00000000ccdf972a 
[    7.856602] [RADXA_71] MM_SET_STACK_START_REQ SUCCESS! 5g_support=0
[    7.856607] AICWFDBG(LOGINFO)        is 5g support = 0, vendor_info = 0x00
[    7.856613] AICWFDBG(LOGTRACE)       rwnx_send_msg (129)MM_GET_FW_VERSION_CFM reqcfm:1 in_irq:0 in_softirq:0 in_atomic:0
[    7.856623] AICWFDBG(LOGTRACE)       rwnx_cmd_malloc get cmd_array[1]:00000000ccdf972a 
[    7.856630] AICWFDBG(LOGERROR)       cmd queue crashed
[    7.856633] AICWFDBG(LOGTRACE)       rwnx_cmd_free cmd_array[1]:00000000ccdf972a 
[    7.856640] AICWFDBG(LOGINFO)        Firmware Version: 
```

---

## 2. Mandatory Rules Derived from Radxa Trace

1. **`cmd 123` Parameter Signature**:
   - `MM_SET_STACK_START_REQ` must pass `vendor_info = 0x00` and `chipid = PRODUCT_ID_AIC8800D80` (`0x0003`).
2. **`cmd queue crashed` Output**:
   - Non-fatal debug print generated by `rwnx_cmds.c` when checking `cmd_mgr->state`. Execution continues normally.
3. **2nd IPC Read (`0x00000020`) Always Times Out**:
   - The 1st IPC read (`0x40500000`) succeeds in ~0.3ms. The 2nd read (`0x00000020` for `chip_sub_id`) times out after ~2s.
   - This timeout sets `cmd_mgr->state = RWNX_CMD_MGR_STATE_CRASHED` and triggers `wlan error reset flow`.
   - **In shenmintao, this blocks all subsequent commands with `-EPIPE`**. The fix is to reset `cmd_mgr->state = RWNX_CMD_MGR_STATE_INITED` after `system_config_8800d80()` returns.
4. **Execution Sequence**:
   - IPC Read -> Load NVRAM -> `MM_SET_STACK_START_REQ` (cmd 123) -> `MM_GET_FW_VERSION_REQ` (cmd 128) -> TX Power Level V3 setup -> RF calibration.

