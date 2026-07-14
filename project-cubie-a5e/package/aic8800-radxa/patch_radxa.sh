#!/bin/bash
set -e

rm -Rf bld/build/aic8800-radxa-main
rm -Rf bld/build/aic8800-radxa-main-orig
mkdir -p bld/build/aic8800-radxa-main
tar --strip-components=1 -C bld/build/aic8800-radxa-main -xf buildroot/dl/aic8800-radxa/aic8800-radxa-main.tar.gz

cp -r bld/build/aic8800-radxa-main bld/build/aic8800-radxa-main-orig

cat << 'EOF' > patch_radxa.py
import re
import glob

# 1. Update ops in rwnx_main.c
file_path = "bld/build/aic8800-radxa-main/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_main.c"
with open(file_path, "r") as f:
    content = f.read()

wrappers = """
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
static int rwnx_cfg80211_get_station_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, const u8 *mac, struct station_info *sinfo) {
    return rwnx_cfg80211_get_station(wiphy, wdev->netdev, mac, sinfo);
}
static int rwnx_cfg80211_dump_station_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, int idx, u8 *mac, struct station_info *sinfo) {
    return rwnx_cfg80211_dump_station(wiphy, wdev->netdev, idx, mac, sinfo);
}
static int rwnx_cfg80211_set_monitor_channel_wrapper(struct wiphy *wiphy, struct net_device *dev, struct cfg80211_chan_def *chandef) {
    return rwnx_cfg80211_set_monitor_channel(wiphy, chandef);
}
static int rwnx_cfg80211_set_wiphy_params_wrapper(struct wiphy *wiphy, int radio_idx, u32 changed) {
    return rwnx_cfg80211_set_wiphy_params(wiphy, changed);
}
static int rwnx_cfg80211_set_tx_power_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, int radio_idx, enum nl80211_tx_power_setting type, int mbm) {
    return rwnx_cfg80211_set_tx_power(wiphy, wdev, type, mbm);
}
static int rwnx_cfg80211_get_tx_power_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, int radio_idx, unsigned int link_id, int *dbm) {
    return rwnx_cfg80211_get_tx_power(wiphy, wdev, dbm);
}
static int rwnx_cfg80211_get_key_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie, void (*callback)(void *cookie, struct key_params *)) {
    return rwnx_cfg80211_get_key(wiphy, wdev->netdev, link_id, key_index, pairwise, mac_addr, cookie, callback);
}
static int rwnx_cfg80211_del_key_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 key_index, bool pairwise, const u8 *mac_addr) {
    return rwnx_cfg80211_del_key(wiphy, wdev->netdev, link_id, key_index, pairwise, mac_addr);
}
static int rwnx_cfg80211_add_key_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 key_index, bool pairwise, const u8 *mac_addr, struct key_params *params) {
    return rwnx_cfg80211_add_key(wiphy, wdev->netdev, link_id, key_index, pairwise, mac_addr, params);
}
static int rwnx_cfg80211_set_default_mgmt_key_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, int link_id, u8 key_index) {
    return rwnx_cfg80211_set_default_mgmt_key(wiphy, wdev->netdev, link_id, key_index);
}
static int rwnx_cfg80211_add_station_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, const u8 *mac, struct station_parameters *params) {
    return rwnx_cfg80211_add_station(wiphy, wdev->netdev, mac, params);
}
static int rwnx_cfg80211_del_station_compat_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, struct station_del_parameters *params) {
    return rwnx_cfg80211_del_station_compat(wiphy, wdev->netdev, params);
}
static int rwnx_cfg80211_change_station_wrapper(struct wiphy *wiphy, struct wireless_dev *wdev, const u8 *mac, struct station_parameters *params) {
    return rwnx_cfg80211_change_station(wiphy, wdev->netdev, mac, params);
}
#endif
"""
content = content.replace("static struct cfg80211_ops rwnx_cfg80211_ops = {", wrappers + "\nstatic struct cfg80211_ops rwnx_cfg80211_ops = {")

for ops in ["set_monitor_channel", "set_wiphy_params", "set_tx_power", "get_tx_power", "get_station", "get_key", "del_key", "add_key", "set_default_mgmt_key", "add_station", "del_station_compat", "change_station"]:
    content = re.sub(r"(\." + ops + r"\s*=\s*)rwnx_cfg80211_" + ops + r",",
                     r"#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n\t\g<1>rwnx_cfg80211_" + ops + r"_wrapper,\n#else\n\t\g<1>rwnx_cfg80211_" + ops + r",\n#endif", content)
    
    # In case it is assigned later (like get_station)
    content = re.sub(r"(rwnx_cfg80211_ops\." + ops + r"\s*=\s*)rwnx_cfg80211_" + ops + r";",
                     r"#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n\t\g<1>rwnx_cfg80211_" + ops + r"_wrapper;\n#else\n\t\g<1>rwnx_cfg80211_" + ops + r";\n#endif", content)

content = re.sub(r"(\.del_station\s*=\s*)rwnx_cfg80211_del_station_compat,",
                 r"#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n\t\g<1>rwnx_cfg80211_del_station_compat_wrapper,\n#else\n\t\g<1>rwnx_cfg80211_del_station_compat,\n#endif", content)

content = re.sub(r"(rwnx_cfg80211_ops\.dump_station\s*=\s*)rwnx_cfg80211_dump_station;",
                 r"#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n\t\g<1>rwnx_cfg80211_dump_station_wrapper;\n#else\n\t\g<1>rwnx_cfg80211_dump_station;\n#endif", content)

content = content.replace("cfg80211_new_sta(rwnx_vif->ndev, sta->mac_addr, &sinfo, GFP_KERNEL);",
    "#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n\t\t\tcfg80211_new_sta(&rwnx_vif->wdev, sta->mac_addr, &sinfo, GFP_KERNEL);\n#else\n\t\t\tcfg80211_new_sta(rwnx_vif->ndev, sta->mac_addr, &sinfo, GFP_KERNEL);\n#endif")

content = content.replace("cfg80211_del_sta(rwnx_vif->ndev, cur->mac_addr, GFP_KERNEL);",
    "#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n\t\t\t\tcfg80211_del_sta(&rwnx_vif->wdev, cur->mac_addr, GFP_KERNEL);\n#else\n\t\t\t\tcfg80211_del_sta(rwnx_vif->ndev, cur->mac_addr, GFP_KERNEL);\n#endif")

with open(file_path, "w") as f:
    f.write(content)

# 2. Add vmalloc.h to all bsp .c files
for file in glob.glob("bld/build/aic8800-radxa-main/src/SDIO/driver_fw/driver/aic8800/aic8800_bsp/*.c"):
    with open(file, "r") as f:
        c = f.read()
    with open(file, "w") as f:
        f.write("#include <linux/vmalloc.h>\n" + c)

# 3. Add timer macros to rwnx_defs.h
file_path = "bld/build/aic8800-radxa-main/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_defs.h"
with open(file_path, "a") as f:
    f.write("\n#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n")
    f.write("#define del_timer(timer) timer_delete(timer)\n")
    f.write("#define del_timer_sync(timer) timer_delete_sync(timer)\n")
    f.write("#define cfg80211_rx_spurious_frame(dev, addr, gfp) (cfg80211_rx_spurious_frame)(dev, addr, 0, gfp)\n")
    f.write("#define rwnx_cfg80211_rx_spurious_frame(dev, addr, gfp) (cfg80211_rx_spurious_frame)(dev, addr, 0, gfp)\n")
    f.write("#define cfg80211_rx_unexpected_4addr_frame(dev, addr, gfp) (cfg80211_rx_unexpected_4addr_frame)(dev, addr, 0, gfp)\n")
    f.write("#define rwnx_cfg80211_rx_unexpected_4addr_frame(dev, addr, gfp) (cfg80211_rx_unexpected_4addr_frame)(dev, addr, 0, gfp)\n")
    f.write("#ifndef from_timer\n")
    f.write("#define from_timer(var, callback_timer, timer_fieldname) container_of(callback_timer, typeof(*var), timer_fieldname)\n")
    f.write("#endif\n")
    f.write("#define ACTION_U action\n")
    f.write("#define wakeup_source_create(name) wakeup_source_register(NULL, name)\n")
    f.write("#define wakeup_source_destroy(ws) wakeup_source_unregister(ws)\n")
    f.write("#define wakeup_source_add(ws)\n")
    f.write("#define wakeup_source_remove(ws)\n")
    f.write("#else\n")
    f.write("#define ACTION_U action.u\n")
    f.write("#endif\n")



# 4. Fix rwnx_tdls.c action union access
file_path = "bld/build/aic8800-radxa-main/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/rwnx_tdls.c"
with open(file_path, "r") as f:
    content = f.read()

content = content.replace(
    "mgmt->u.action.u.tdls_discover_resp.action_code = WLAN_PUB_ACTION_TDLS_DISCOVER_RES;",
    "#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)\n"
    "\t\tmgmt->u.action.action_code = WLAN_PUB_ACTION_TDLS_DISCOVER_RES;\n"
    "#else\n"
    "\t\tmgmt->u.action.u.tdls_discover_resp.action_code = WLAN_PUB_ACTION_TDLS_DISCOVER_RES;\n"
    "#endif"
)
content = content.replace("u.action.u.", "u.ACTION_U.")

with open(file_path, "w") as f:
    f.write(content)

# 5. Fix Firmware Path
file_path = "bld/build/aic8800-radxa-main/src/SDIO/driver_fw/driver/aic8800/aic8800_bsp/Makefile"
with open(file_path, "r") as f:
    content = f.read()

content = content.replace('CONFIG_AIC_FW_PATH = "/vendor/etc/firmware"', 'CONFIG_AIC_FW_PATH = "/lib/firmware/aic8800D80"')

with open(file_path, "w") as f:
    f.write(content)

EOF

python3 patch_radxa.py

diff -urN bld/build/aic8800-radxa-main-orig/src/SDIO/driver_fw/driver/aic8800 bld/build/aic8800-radxa-main/src/SDIO/driver_fw/driver/aic8800 > cubie-a5e/project-cubie-a5e/package/aic8800-radxa/0001-kernel-7.1-cfg80211-ops.patch || true

sed -i 's|--- bld/build/aic8800-radxa-main-orig/|--- a/|g' cubie-a5e/project-cubie-a5e/package/aic8800-radxa/0001-kernel-7.1-cfg80211-ops.patch
sed -i 's|+++ bld/build/aic8800-radxa-main/|+++ b/|g' cubie-a5e/project-cubie-a5e/package/aic8800-radxa/0001-kernel-7.1-cfg80211-ops.patch
