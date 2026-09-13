#!/bin/bash
# ==============================================================================
# check_upstream_rfc.sh - Verify and Dry-Run Upstream RFC Patch Series
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PATCH_DIR="${REPO_ROOT}/patches-upstream-rfc"
CHECKPATCH="${REPO_ROOT}/../bld.a5e/build/linux-7.1/scripts/checkpatch.pl"

if [ ! -f "${CHECKPATCH}" ]; then
    CHECKPATCH="checkpatch.pl"
fi

echo "======================================================================"
echo "  Validating Upstream RFC Patch Series for linux-sunxi / linux-remoteproc"
echo "  Directory: ${PATCH_DIR}"
echo "======================================================================"

total_errors=0

for p in "${PATCH_DIR}"/*.patch; do
    fname=$(basename "$p")
    echo -n "Checking ${fname}... "
    
    # Run checkpatch
    set +e
    out=$(${CHECKPATCH} --no-tree "$p" 2>&1)
    ret=$?
    set -e
    
    if echo "$out" | grep -q "total: 0 errors"; then
        echo -e "\033[32m[PASS]\033[0m"
    else
        echo -e "\033[31m[FAIL]\033[0m"
        echo "$out"
        total_errors=$((total_errors + 1))
    fi
done

echo "======================================================================"
if [ $total_errors -eq 0 ]; then
    echo -e "\033[32m>>> ALL PATCHES PASSED CHECKPATCH! Ready for RFC submission. <<<\033[0m"
    echo ""
    echo "To send this series via git send-email:"
    echo "----------------------------------------------------------------------"
    echo "git send-email \\"
    echo "  --to=\"linux-sunxi@lists.linux.dev\" \\"
    echo "  --to=\"linux-remoteproc@vger.kernel.org\" \\"
    echo "  --cc=\"linux-arm-kernel@lists.infradead.org\" \\"
    echo "  --cc=\"devicetree@vger.kernel.org\" \\"
    echo "  --cc=\"jernej.skrabec@gmail.com\" \\"
    echo "  --cc=\"samuel@sholland.org\" \\"
    echo "  --cc=\"mathieu.poirier@linaro.org\" \\"
    echo "  --cc=\"andersson@kernel.org\" \\"
    echo "  --cc=\"jaswinder.singh@linaro.org\" \\"
    echo "  --cc=\"robh@kernel.org\" \\"
    echo "  --cc=\"krzk+dt@kernel.org\" \\"
    echo "  ${PATCH_DIR}/0000-cover-letter.patch \\"
    echo "  ${PATCH_DIR}/0001-*.patch \\"
    echo "  ${PATCH_DIR}/0002-*.patch \\"
    echo "  ${PATCH_DIR}/0003-*.patch \\"
    echo "  ${PATCH_DIR}/0004-*.patch \\"
    echo "  ${PATCH_DIR}/0005-*.patch"
    echo "----------------------------------------------------------------------"
else
    echo -e "\033[31m>>> FAILED: ${total_errors} patches have checkpatch errors. <<<\033[0m"
    exit 1
fi
