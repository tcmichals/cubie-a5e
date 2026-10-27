# remoteproc framework feature matrix vs sunxi_rproc (linux-cubie, cubie-linux-7.1)

Method: features enumerated from include/linux/remoteproc.h (struct rproc_ops: 18 fields;
struct rproc driver-settable fields), every EXPORT_SYMBOL in drivers/remoteproc/remoteproc_*.c
(33 APIs), plus driver-model hooks (dev_pm_ops, .shutdown, remove ordering, devm idioms).
Peers: all 26 platform driver files in drivers/remoteproc/ (qcom_common/qcom_q6v5 counted as
helpers of the qcom drivers; ti_k3_common as helper of the K3 drivers). Matching is grep-based
(file-level), then every gap below was confirmed by reading the peer code and the core path.
"# peers" excludes sunxi. 69 rows.

## Matrix

| Feature | sunxi | # peers | Peer drivers |
|---|---|---|---|
| ops.prepare | YES | 10 | imx_rproc imx_dsp_rproc stm32_rproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc qcom_common mtk_scp xlnx_r5_remoteproc ingenic_rproc rcar_rproc |
| ops.unprepare | YES | 8 | imx_dsp_rproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc qcom_q6v5_pas qcom_common mtk_scp xlnx_r5_remoteproc ingenic_rproc |
| ops.start | YES | 24 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc da8xx_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss qcom_common mtk_scp meson_mx_ao_arc xlnx_r5_remoteproc pru_rproc wkup_m3_rproc ingenic_rproc rcar_rproc |
| ops.stop | YES | 24 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc da8xx_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss qcom_common mtk_scp meson_mx_ao_arc xlnx_r5_remoteproc pru_rproc wkup_m3_rproc ingenic_rproc rcar_rproc |
| ops.attach | no | 5 | imx_rproc stm32_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc xlnx_r5_remoteproc |
| ops.detach | no | 5 | imx_rproc stm32_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc xlnx_r5_remoteproc |
| ops.kick | YES | 12 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc da8xx_remoteproc xlnx_r5_remoteproc ingenic_rproc |
| ops.da_to_va | YES | 16 | imx_rproc st_slim_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc qcom_q6v5_adsp qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss mtk_scp meson_mx_ao_arc pru_rproc wkup_m3_rproc ingenic_rproc |
| ops.parse_fw | YES | 12 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss mtk_scp xlnx_r5_remoteproc rcar_rproc |
| ops.handle_rsc | no | 1 | imx_dsp_rproc |
| ops.find_loaded_rsc_table | YES | 6 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc xlnx_r5_remoteproc rcar_rproc |
| ops.get_loaded_rsc_table | no | 5 | imx_rproc stm32_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc xlnx_r5_remoteproc |
| ops.load | YES | 14 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss mtk_scp meson_mx_ao_arc xlnx_r5_remoteproc rcar_rproc |
| ops.sanity_check | YES | 9 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc mtk_scp meson_mx_ao_arc xlnx_r5_remoteproc rcar_rproc |
| ops.get_boot_addr | YES | 9 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc qcom_q6v5_wcss meson_mx_ao_arc xlnx_r5_remoteproc rcar_rproc |
| ops.panic | no | 3 | qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas |
| ops.coredump | no | 1 | qcom_q6v5_pas |
| rproc_alloc (non-devm) | no | 2 | st_slim_rproc xlnx_r5_remoteproc |
| devm_rproc_alloc | YES | 21 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc da8xx_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss mtk_scp meson_mx_ao_arc pru_rproc wkup_m3_rproc ingenic_rproc rcar_rproc |
| rproc_add (non-devm) | YES | 12 | stm32_rproc st_remoteproc st_slim_rproc da8xx_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss mtk_scp meson_mx_ao_arc xlnx_r5_remoteproc |
| devm_rproc_add | no | 12 | imx_rproc imx_dsp_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc da8xx_remoteproc pru_rproc wkup_m3_rproc ingenic_rproc rcar_rproc |
| rproc_del | YES | 11 | stm32_rproc st_remoteproc st_slim_rproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss mtk_scp meson_mx_ao_arc xlnx_r5_remoteproc |
| rproc_free | no | 2 | st_slim_rproc xlnx_r5_remoteproc |
| rproc_coredump_set_elf_info | YES | 7 | imx_dsp_rproc stm32_rproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_wcnss xlnx_r5_remoteproc |
| rproc_coredump_add_segment | no | 5 | imx_rproc imx_dsp_rproc stm32_rproc qcom_common xlnx_r5_remoteproc |
| rproc_coredump_add_custom_segment | no | 2 | qcom_q6v5_mss qcom_common |
| rproc_coredump_using_sections | no | 1 | qcom_common |
| rproc_of_parse_firmware | YES | 7 | imx_dsp_rproc stm32_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc mtk_scp pru_rproc |
| rproc_of_resm_mem_entry_init | no | 3 | stm32_rproc st_remoteproc xlnx_r5_remoteproc |
| rproc_mem_entry_init | YES | 6 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc xlnx_r5_remoteproc rcar_rproc |
| rproc_add_carveout | YES | 6 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc xlnx_r5_remoteproc rcar_rproc |
| rproc_mem_entry_init with alloc/release callbacks | no | 6 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc rcar_rproc xlnx_r5_remoteproc |
| rproc_report_crash | YES | 7 | imx_rproc stm32_rproc keystone_remoteproc omap_remoteproc qcom_wcnss qcom_q6v5 mtk_scp |
| rproc_vq_interrupt | YES | 10 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc ti_k3_common keystone_remoteproc omap_remoteproc da8xx_remoteproc xlnx_r5_remoteproc ingenic_rproc |
| rproc_add_subdev | no | 2 | qcom_common mtk_scp |
| rproc_set_feature | no | 1 | imx_rproc |
| rproc_elf_load_rsc_table | YES | 9 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc qcom_q6v5_adsp qcom_q6v5_pas xlnx_r5_remoteproc pru_rproc rcar_rproc |
| rproc_elf_find_loaded_rsc_table | YES | 6 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc xlnx_r5_remoteproc rcar_rproc |
| rproc_elf_load_segments | YES | 8 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc meson_mx_ao_arc xlnx_r5_remoteproc rcar_rproc |
| rproc_elf_sanity_check | YES | 9 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc mtk_scp meson_mx_ao_arc xlnx_r5_remoteproc rcar_rproc |
| rproc_elf_get_boot_addr | YES | 9 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc st_slim_rproc qcom_q6v5_wcss meson_mx_ao_arc xlnx_r5_remoteproc rcar_rproc |
| rproc_da_to_va (caller) | no | 2 | imx_dsp_rproc mtk_scp |
| rproc_get_by_phandle | no | 1 | pru_rproc |
| rproc_get_by_child | no | 0 |  |
| rproc_boot/shutdown in driver | no | 2 | stm32_rproc xlnx_r5_remoteproc |
| rproc_detach in driver | no | 6 | imx_rproc stm32_rproc ti_k3_common ti_k3_dsp_remoteproc ti_k3_r5_remoteproc xlnx_r5_remoteproc |
| rproc->auto_boot | no | 12 | imx_rproc imx_dsp_rproc stm32_rproc ti_k3_r5_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas xlnx_r5_remoteproc pru_rproc wkup_m3_rproc ingenic_rproc rcar_rproc |
| rproc->recovery_disabled | no | 7 | imx_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc da8xx_remoteproc xlnx_r5_remoteproc pru_rproc |
| rproc->has_iommu | no | 13 | stm32_rproc st_remoteproc st_slim_rproc ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc da8xx_remoteproc qcom_q6v5_adsp qcom_q6v5_pas meson_mx_ao_arc xlnx_r5_remoteproc |
| rproc->state (attach detection) | no | 11 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc ti_k3_common ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc omap_remoteproc mtk_scp xlnx_r5_remoteproc |
| rproc->sysfs_read_only | no | 2 | pru_rproc wkup_m3_rproc |
| rproc->dump_conf | no | 1 | qcom_q6v5_pas |
| rproc->table_ptr/table_sz/cached_table | no | 4 | imx_rproc stm32_rproc ti_k3_common qcom_q6v5_pas |
| dev_pm_ops / SIMPLE_DEV_PM_OPS | no | 5 | imx_dsp_rproc stm32_rproc omap_remoteproc mtk_scp wkup_m3_rproc |
| system suspend/resume callbacks | no | 5 | imx_dsp_rproc stm32_rproc omap_remoteproc mtk_scp wkup_m3_rproc |
| .shutdown platform cb | no | 1 | xlnx_r5_remoteproc |
| pm_runtime_* | YES | 10 | imx_rproc imx_dsp_rproc keystone_remoteproc omap_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_wcnss wkup_m3_rproc rcar_rproc |
| (devm_)dev_pm_domain_attach_list | YES | 3 | imx_rproc imx_dsp_rproc qcom_q6v5_adsp |
| of_reserved_mem_device_init | YES | 5 | ti_k3_common keystone_remoteproc omap_remoteproc da8xx_remoteproc mtk_scp |
| of_reserved_mem_region_*/of_reserved_mem_lookup | YES | 12 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc ti_k3_common qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_pas qcom_q6v5_wcss qcom_wcnss xlnx_r5_remoteproc rcar_rproc |
| devm_add_action (custom cleanup) | no | 10 | imx_rproc ti_k3_common ti_k3_dsp_remoteproc ti_k3_m4_remoteproc ti_k3_r5_remoteproc keystone_remoteproc omap_remoteproc da8xx_remoteproc xlnx_r5_remoteproc wkup_m3_rproc |
| mbox_request_channel | YES | 7 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc ti_k3_common omap_remoteproc xlnx_r5_remoteproc |
| mbox_free_channel | YES | 7 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc ti_k3_common omap_remoteproc xlnx_r5_remoteproc |
| workqueue for rx (queue_work) | YES | 5 | imx_rproc imx_dsp_rproc stm32_rproc keystone_remoteproc xlnx_r5_remoteproc |
| dma_set_coherent_mask/dma_set_mask | YES | 2 | omap_remoteproc xlnx_r5_remoteproc |
| syscon / regmap | no | 10 | imx_rproc imx_dsp_rproc stm32_rproc st_remoteproc keystone_remoteproc omap_remoteproc qcom_q6v5_adsp qcom_q6v5_mss qcom_q6v5_wcss meson_mx_ao_arc |
| device_link | no | 0 |  |
| watchdog / RPROC_WATCHDOG | no | 6 | imx_rproc stm32_rproc omap_remoteproc qcom_wcnss qcom_q6v5 mtk_scp |
| RPROC_FATAL_ERROR | YES | 4 | keystone_remoteproc omap_remoteproc qcom_wcnss qcom_q6v5 |

Not used by any platform driver (core/consumer API only): rproc_get_by_child, rproc_put,
rproc_set_firmware, rproc_coredump (default ops->coredump), rproc_resource_cleanup,
rproc_coredump_cleanup, rproc_remove_subdev (only with add_subdev), rproc_boot/shutdown/detach
from consumers.

## Classified gaps and idiom differences

### NEEDED

N1. **Coredump has no segments: rproc_coredump_add_segment() never called** (precedent:
imx_rproc [prepare, per memory-region], imx_dsp_rproc [SRAM ATT_OWN windows *and* memory-regions],
stm32_rproc [memory-regions], xlnx_r5_remoteproc [TCM banks + memory-regions], qcom_common
[per ELF phdr]; 5 drivers, every non-qcom driver that calls set_elf_info also adds segments).
Core path: rproc_boot_recovery() -> rproc_stop() -> rproc->ops->coredump == rproc_coredump()
(default installed in rproc_alloc_ops, remoteproc_core.c:2420); rproc_coredump() returns
immediately on `list_empty(&rproc->dump_segments)` (remoteproc_coredump.c:248) **before** the
elf_class check, and nothing in the core ever adds a segment (no add_segment call in
remoteproc_core.c / _elf_loader.c / _virtio.c). Scenario: `echo enabled >
/sys/kernel/debug/remoteproc/remoteproc0/coredump`, firmware crashes (crash IRQ or debugfs
`crash`), recovery runs, no devcoredump appears; the rproc_coredump_set_elf_info() call added in
QUAL-1 is dead code. Note: with an empty list the "ELF class is not set" message cannot be
reached either, so the QUAL-1 diagnosis in TODO.md was incomplete. rproc_resource_cleanup()
calls rproc_coredump_cleanup() after every shutdown, so segments must be re-added each boot:
add them in sunxi_rproc_prepare() next to rproc_add_carveout() (imx_rproc idiom): both SRAM
windows by their E906 DA (E906_SRAM_SPACE0_DA/1_DA, resolved by da_to_va with is_iomem, which
rproc_copy_segment() handles with memcpy_fromio) and each carveout by its da. The dump is taken
after ops->stop() only (no unprepare in recovery), so clocks/remap are still on and SRAM is
readable. Touches D7 (DRAM carveouts) only in that they become dump segments too.

N2. **No system-sleep handling; genpd powers off PD_RISCV and PD_SRAM under a running core**
(precedent: imx_dsp_rproc [SYSTEM_SLEEP_PM_OPS: notify, pm_runtime_force_suspend, reload
firmware on resume], omap_remoteproc [suspend/resume with firmware handshake], mtk_scp
[drop/retake clock when RUNNING], stm32_rproc [wake IRQ], wkup_m3; 5 drivers). Core path:
devm_pm_domain_attach_list() registers genpd virtual devices (genpd_dev_pm_attach_by_id), which
are on the dpm list; at suspend_noirq genpd_finish_suspend() -> genpd_sync_power_off()
(drivers/pmdomain/core.c) powers the domain off whenever suspended_count == device_count,
regardless of runtime-PM usage counts. The sun20i PPU driver sets no GENPD_FLAG_ALWAYS_ON /
active-wakeup, and our virtual devices are the only users of PD_RISCV and PD_SRAM in
sun55i-a523.dtsi. Scenario: E906 running rpmsg, `echo freeze > /sys/power/state` (s2idle is
always available) -> both domains off, SRAM contents lost; on resume the domains come back with
the core reset still deasserted, so the E906 executes garbage SRAM while rproc->state is still
RUNNING and rpmsg/vrings are stale; no error is reported. Minimum fix: dev_pm_ops whose suspend
either refuses (-EBUSY) while RUNNING or stops the core and restarts/reloads on resume
(imx_dsp_rproc model). Already a planned item in TODO.md "Task 3: System Power Management", but
it is a correctness gap for the upstream driver, not a feature: should be done or the
limitation at least handled (refuse suspend) before v3. Verify on the board with s2idle.

### RECOMMENDED

R1. **firmware-name parse errors are ignored** (precedent: stm32_rproc `if (ret < 0 && ret !=
-EINVAL) return ret;`; imx_dsp_rproc, pru_rproc, ti_k3_* return the error; mtk_scp falls back
deliberately). rproc_of_parse_firmware() returns of_property_read_string_index()'s error
(-EINVAL absent, -ENODATA/-EILSEQ malformed). Scenario: a malformed firmware-name silently
boots riscv-firmware.elf. Follow stm32: ignore only -EINVAL.

R2. **Reserved-memory release by devm action instead of manual goto + remove()** (precedent:
ti_k3_common k3_mem_release, omap_remoteproc omap_rproc_mem_release, keystone, da8xx use
devm_add_action_or_reset(..., of_reserved_mem_device_release)). Not a bug today (paths are
correct) but it removes has_reserved_mem and both error labels; maintainers (Mathieu) have asked
for this pattern in recent drivers. Low priority.

R3. **.shutdown platform callback** (precedent: xlnx_r5_remoteproc
zynqmp_r5_remoteproc_shutdown: rproc_shutdown()/rproc_detach() + free mbox; qcom_q6v5_adsp/pas).
Scenario: kexec with the E906 running: it keeps executing and kicking msgbox channel 9 (and
writing the vram pool) in the new kernel until our prepare() asserts the core reset; the new
kernel's msgbox driver sees interrupts for a channel nobody owns. PSCI reboot resets the SoC, so
only kexec is affected. Optional; mention in cover letter if not done.

### DECISION / NOT APPLICABLE (checked, no action)

- ops.attach/.detach, ops.get_loaded_rsc_table, rproc->state = RPROC_DETACHED detection,
  RPROC_FEAT_ATTACH_ON_RECOVERY/rproc_set_feature (imx_rproc, stm32, ti_k3_*, xlnx_r5): N/A,
  Linux always loads the E906; prepare() deliberately asserts the core reset to take over a core
  a bootloader left running. Could become a feature later (U-Boot-started firmware), not a gap.
- ops.handle_rsc (imx_dsp only, vendor resource), ops.panic (qcom only, needs firmware panic
  handshake), ops.coredump custom (qcom_pas): N/A.
- rproc_coredump_add_custom_segment / rproc_coredump_using_sections (qcom): N/A, plain memory
  segments suffice (see N1).
- rproc_of_resm_mem_entry_init (stm32, st, xlnx_r5 for vdev0buffer): N/A, D10 (index-0 pool).
- rproc_mem_entry_init alloc/release callbacks (imx, imx_dsp, stm32, st, rcar, xlnx_r5 map per
  boot): we premap at probe (devm_memremap WC / SRAM ioremap) and pass NULL callbacks; valid,
  core only calls alloc when non-NULL. D7 / C1 (WC mapping). No action.
- rproc->auto_boot (12 drivers set it, mostly to false or from a DT prop): we keep the default
  true like st, st_slim, meson, mtk_scp, omap, keystone, da8xx, qcom_wcnss. No action; note a
  built-in driver may try request_firmware before rootfs (only a log line).
- rproc->recovery_disabled (7 drivers, mostly multi-core/lockstep or no crash source): default
  recovery works with our ops (rproc_boot_recovery -> stop (core reset, IRQ disarmed) ->
  rproc_start reload; carveouts persist since no resource_cleanup; elf loader memset_io's .bss
  in iomem; start re-arms crash IRQ). D14. No action.
- rproc->has_iommu (13 drivers set it, mostly false explicitly): default false is correct.
- rproc->sysfs_read_only (pru, wkup_m3: in-kernel consumer owns boot): N/A.
- rproc->dump_conf (qcom_pas only): default DISABLED is the norm; users enable via debugfs.
- rproc_add_subdev (qcom glink/smd/ssr, mtk_scp rpmsg): N/A, virtio subdevs come from the core.
- rproc_get_by_phandle (pru consumers): N/A.
- RPROC_WATCHDOG / watchdog IRQ (imx, stm32, omap, qcom_wcnss, qcom_q6v5, mtk_scp): N/A, no
  watchdog line routed to the A55 for the E906; crash IRQ uses RPROC_FATAL_ERROR (D14).
- device_init_wakeup / pm_stay_awake: rproc_report_crash() calls pm_stay_awake(parent); a no-op
  without a wakeup source; only stm32/qcom_pas set one. N/A.
- syscon/regmap (10 drivers): remap register is a raw 4-byte map, D9 (offer sunxi SRAM driver).
- Graceful stop handshake before reset (stm32 shutdown channel, imx_dsp/omap messages): N/A
  today, E906 D-cache is off (C1), nothing to flush; firmware-defined later if needed.
- devm_rproc_add (21 peers use devm_rproc_alloc, ~10 use devm_rproc_add): we use
  rproc_add()/rproc_del() so the mailbox outlives the vdevs; same as stm32, mtk_scp, meson,
  qcom_*. Correct ordering, no action.

### Used and matching the common idiom (no action)
prepare/unprepare (12/9 peers), start/stop, kick, da_to_va (16), parse_fw with
rproc_elf_load_rsc_table tolerating -EINVAL (imx tolerates all errors), find_loaded_rsc_table,
load = rproc_elf_load_segments, sanity_check wrapping rproc_elf_sanity_check (9),
get_boot_addr = rproc_elf_get_boot_addr, rproc_coredump_set_elf_info (7), rproc_mem_entry_init +
rproc_add_carveout in prepare (imx_rproc idiom, 6), rproc_report_crash (7), rproc_vq_interrupt
over idr_for_each (imx_rproc idiom), rx via ordered workqueue (imx, imx_dsp, stm32, xlnx),
mbox_request/free_channel, of_reserved_mem_device_init for the pool (ti_k3, omap, keystone,
da8xx, mtk_scp), dma_set_coherent_mask (omap, xlnx_r5), devm_pm_domain_attach_list (imx,
imx_dsp, qcom_adsp), runtime PM get in prepare/put in unprepare.
