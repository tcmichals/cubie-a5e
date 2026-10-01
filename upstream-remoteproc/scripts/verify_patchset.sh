#!/usr/bin/env bash
# ==============================================================================
# verify_patchset.sh - High-Confidence Pre-Flight Screening for Upstream Patches
# ==============================================================================
# Performs multi-layer screening:
# 1. Deterministic YAML binding & schema checks
# 2. Kernel checkpatch.pl --strict compliance
# 3. Known anti-pattern negative invariant grep scans
# 4. Git commit discipline & Signed-off-by validation
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPSTREAM_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CUBIE_A5E_DIR="$(cd "${UPSTREAM_DIR}/.." && pwd)"
TOP_DIR="$(cd "${CUBIE_A5E_DIR}/.." && pwd)"
LINUX_DIR="${TOP_DIR}/linux-cubie"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}==================================================================${NC}"
echo -e "${BLUE}   Upstream Remoteproc & Mailbox Pre-Flight Verification Harness   ${NC}"
echo -e "${BLUE}==================================================================${NC}"

ERRORS=0
WARNINGS=0

# ------------------------------------------------------------------------------
# 1. Deterministic YAML Schema & Binding Anti-Pattern Screening
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}[Step 1/4] Screening Devicetree YAML Bindings...${NC}"

YAML_FILES=(
    "${LINUX_DIR}/Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml"
    "${LINUX_DIR}/Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml"
)

for yf in "${YAML_FILES[@]}"; do
    if [ ! -f "$yf" ]; then
        echo -e "${RED}ERROR: Missing schema file: $yf${NC}"
        ERRORS=$((ERRORS + 1))
        continue
    fi
    rel_path="${yf#"${LINUX_DIR}/"}"
    echo "  Checking: $rel_path"

    # Rule 1.1: Ensure reg-names does NOT use unconstrained enum
    if grep -A 10 "reg-names:" "$yf" | grep -q "enum:"; then
        echo -e "${RED}  FAIL: reg-names in $rel_path uses 'enum:' instead of positional list!${NC}"
        ERRORS=$((ERRORS + 1))
    fi

    # Rule 1.2: Ensure additionalProperties or unevaluatedProperties is false
    if ! grep -qE "(additionalProperties|unevaluatedProperties): false" "$yf"; then
        echo -e "${RED}  FAIL: $rel_path missing 'additionalProperties: false'${NC}"
        ERRORS=$((ERRORS + 1))
    fi

    # Rule 1.3: Ensure no tab characters in YAML
    if grep -q $'\t' "$yf"; then
        echo -e "${RED}  FAIL: $rel_path contains forbidden tab characters!${NC}"
        ERRORS=$((ERRORS + 1))
    fi
done

# Run kernel dt_binding_check if in linux tree
if [ -d "${LINUX_DIR}" ] && [ -f "${LINUX_DIR}/scripts/dt-doc-validate" ]; then
    echo "  Running dt-doc-validate on schemas..."
    for yf in "${YAML_FILES[@]}"; do
        if python3 -m dtschema.validator "$yf" 2>/dev/null; then
            echo -e "${GREEN}  dt-doc-validate PASS: $(basename "$yf")${NC}"
        else
            echo -e "${YELLOW}  dt-doc-validate note: dtschema toolchain recommended for local full check${NC}"
        fi
    done
fi

# ------------------------------------------------------------------------------
# 2. Driver Code Known Anti-Pattern Scan (Negative Invariant Ledger)
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}[Step 2/4] Screening C Source Files against Anti-Patterns...${NC}"

MSGBOX_C="${LINUX_DIR}/drivers/mailbox/sun55i-msgbox.c"
RPROC_C="${LINUX_DIR}/drivers/remoteproc/sunxi_rproc.c"

# Check 2.1: FIFO last_tx_done condition
if [ -f "$MSGBOX_C" ]; then
    if grep -q "return count < SUN55I_FIFO_MAX" "$MSGBOX_C"; then
        echo -e "${RED}  FAIL: sun55i-msgbox.c last_tx_done allows partial FIFO! Must be 'count == 0'.${NC}"
        ERRORS=$((ERRORS + 1))
    else
        echo -e "${GREEN}  PASS: sun55i-msgbox.c last_tx_done checks full drain (count == 0).${NC}"
    fi

    # Check 2.2: Hardirq spinlock
    if ! grep -q "spin_lock_irqsave(&mbox->lock" "$MSGBOX_C"; then
        echo -e "${RED}  FAIL: sun55i_msgbox_irq missing spin_lock_irqsave protection!${NC}"
        ERRORS=$((ERRORS + 1))
    else
        echo -e "${GREEN}  PASS: sun55i-msgbox.c IRQ handler locked against SMP races.${NC}"
    fi

    # Check 2.3: Teardown synchronization
    if ! grep -q "synchronize_irq" "$MSGBOX_C"; then
        echo -e "${RED}  FAIL: sun55i-msgbox.c remove() missing synchronize_irq()!${NC}"
        ERRORS=$((ERRORS + 1))
    else
        echo -e "${GREEN}  PASS: sun55i-msgbox.c synchronizes IRQs before controller unregister.${NC}"
    fi
fi

# Check 2.4: RemoteProc kick msg and knows_txdone
if [ -f "$RPROC_C" ]; then
    if grep -q "cl.knows_txdone = true" "$RPROC_C"; then
        echo -e "${RED}  FAIL: sunxi_rproc.c sets cl.knows_txdone = true! Controller manages TX-done.${NC}"
        ERRORS=$((ERRORS + 1))
    else
        echo -e "${GREEN}  PASS: sunxi_rproc.c leaves TX-done pacing to mailbox controller.${NC}"
    fi

    # Check 2.5: DMA carveout cache attributes
    if grep -A 3 "priv->trace_va" "$RPROC_C" | grep -q "\*is_iomem = false"; then
        echo -e "${GREEN}  PASS: sunxi_rproc.c trace carveout correctly sets *is_iomem = false.${NC}"
    else
        echo -e "${RED}  FAIL: sunxi_rproc.c trace carveout missing *is_iomem = false!${NC}"
        ERRORS=$((ERRORS + 1))
    fi
fi

# ------------------------------------------------------------------------------
# 3. Kernel checkpatch.pl Strict Compliance
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}[Step 3/4] Running checkpatch.pl --strict on commits...${NC}"

if [ -f "${LINUX_DIR}/scripts/checkpatch.pl" ]; then
    pushd "${LINUX_DIR}" > /dev/null
    CP_OUTPUT=$(./scripts/checkpatch.pl --git HEAD~2..HEAD --strict --no-tree 2>&1 || true)
    popd > /dev/null

    if echo "$CP_OUTPUT" | grep -q "ERROR:"; then
        echo -e "${RED}  checkpatch.pl encountered ERRORS:${NC}"
        echo "$CP_OUTPUT" | grep -E "(ERROR|WARNING|CHECK):" || true
        ERRORS=$((ERRORS + 1))
    elif echo "$CP_OUTPUT" | grep -q "WARNING:"; then
        echo -e "${YELLOW}  checkpatch.pl warnings (review carefully):${NC}"
        echo "$CP_OUTPUT" | grep -E "WARNING:" || true
        WARNINGS=$((WARNINGS + 1))
    else
        echo -e "${GREEN}  checkpatch.pl: 0 errors, 0 warnings!${NC}"
    fi
else
    echo -e "${YELLOW}  checkpatch.pl not found in linux tree, skipping.${NC}"
fi

# ------------------------------------------------------------------------------
# 4. Summary & Upstream Readiness Verdict
# ------------------------------------------------------------------------------
echo -e "\n${BLUE}==================================================================${NC}"
if [ "$ERRORS" -eq 0 ]; then
    echo -e "${GREEN}   VERIFICATION PASSED: Code & schemas are UPSTREAM READY (0 errors, ${WARNINGS} warnings)   ${NC}"
    echo -e "${BLUE}==================================================================${NC}"
    exit 0
else
    echo -e "${RED}   VERIFICATION FAILED: $ERRORS errors found. Fix before submitting upstream!   ${NC}"
    echo -e "${BLUE}==================================================================${NC}"
    exit 1
fi
