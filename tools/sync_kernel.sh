#!/usr/bin/env bash
# ==============================================================================
# sync_kernel.sh
# Quick Helper for Multi-PC Kernel Sync & Buildroot Rebuilds
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
KERNEL_DIR="${WORKSPACE_ROOT}/linux-cubie"
KERNEL_BRANCH="cubie-linux-7.1"

usage() {
    echo "Usage: $0 [push|pull|rebuild|status]"
    echo ""
    echo "Commands:"
    echo "  push     - Commit-check and push kernel branch (${KERNEL_BRANCH}) to GitHub"
    echo "  pull     - Pull latest kernel changes from GitHub and trigger rebuild"
    echo "  rebuild  - Trigger Buildroot kernel incremental rebuild (bld.a5e & bld.a7a)"
    echo "  status   - Show Git status for both cubie-a5e and linux-cubie"
    exit 1
}

CMD="${1:-status}"

case "${CMD}" in
    push)
        echo "==> Pushing linux-cubie (${KERNEL_BRANCH}) to GitHub..."
        git -C "${KERNEL_DIR}" push origin "${KERNEL_BRANCH}"
        echo "✅ Kernel pushed successfully!"
        ;;
    pull)
        echo "==> Pulling latest linux-cubie (${KERNEL_BRANCH}) from GitHub..."
        git -C "${KERNEL_DIR}" pull origin "${KERNEL_BRANCH}"
        echo "==> Rebuilding kernel in bld.a5e..."
        if [ -d "${WORKSPACE_ROOT}/bld.a5e" ]; then
            make -C "${WORKSPACE_ROOT}/bld.a5e" linux-rebuild
        fi
        echo "✅ Pull and rebuild complete!"
        ;;
    rebuild)
        echo "==> Rebuilding kernel in bld.a5e..."
        if [ -d "${WORKSPACE_ROOT}/bld.a5e" ]; then
            make -C "${WORKSPACE_ROOT}/bld.a5e" linux-rebuild
        fi
        if [ -d "${WORKSPACE_ROOT}/bld.a7a" ]; then
            echo "==> Rebuilding kernel in bld.a7a..."
            make -C "${WORKSPACE_ROOT}/bld.a7a" linux-rebuild
        fi
        echo "✅ Rebuild complete!"
        ;;
    status)
        echo "======================================================================"
        echo " [1] cubie-a5e status:"
        echo "======================================================================"
        git -C "${SCRIPT_DIR}/.." status -s -b
        echo ""
        echo "======================================================================"
        echo " [2] linux-cubie status:"
        echo "======================================================================"
        if [ -d "${KERNEL_DIR}/.git" ]; then
            git -C "${KERNEL_DIR}" status -s -b
            echo ""
            echo "Recent commits in linux-cubie:"
            git -C "${KERNEL_DIR}" log -n 3 --oneline
        else
            echo "linux-cubie directory not found at ${KERNEL_DIR}"
        fi
        echo "======================================================================"
        ;;
    *)
        usage
        ;;
esac
