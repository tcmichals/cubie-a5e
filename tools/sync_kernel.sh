#!/usr/bin/env bash
# ==============================================================================
# sync_kernel.sh
# Quick Helper for Multi-PC Kernel Sync & Buildroot Rebuilds
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# Each board has its own kernel tree and branch (.agents/rules/kernel-branch-discipline.md).
A5E_KERNEL_DIR="${WORKSPACE_ROOT}/linux-v4-71"
A5E_KERNEL_BRANCH="cubie-linux-7.1"
A7A_KERNEL_DIR="${WORKSPACE_ROOT}/linux-cubie"
A7A_KERNEL_BRANCH="a7a_remoteproc_camera_usb"

usage() {
    echo "Usage: $0 [push|pull|rebuild|status]"
    echo ""
    echo "Commands:"
    echo "  push     - Push both kernel branches (${A5E_KERNEL_BRANCH}, ${A7A_KERNEL_BRANCH}) to GitHub"
    echo "  pull     - Pull latest kernel changes from GitHub and trigger rebuild"
    echo "  rebuild  - Trigger Buildroot kernel incremental rebuild (bld.a5e & bld.a7a)"
    echo "  status   - Show Git status for both cubie-a5e and linux-cubie"
    exit 1
}

# A tree on the wrong branch would have the other board's commits merged into it.
require_branch() {
    local current
    current="$(git -C "$1" branch --show-current)"
    if [ "${current}" != "$2" ]; then
        echo "ERROR: $1 is on '${current}', expected '$2'. Nothing was pulled." >&2
        exit 1
    fi
}

show_tree() {
    if [ -e "$1/.git" ]; then
        git -C "$1" status -s -b
        [ "$(git -C "$1" branch --show-current)" = "$2" ] || echo "WARNING: expected branch $2"
        echo ""
        echo "Recent commits:"
        git -C "$1" log -n 3 --oneline
    else
        echo "directory not found: $1"
    fi
}

CMD="${1:-status}"

case "${CMD}" in
    push)
        echo "==> Pushing ${A5E_KERNEL_BRANCH} and ${A7A_KERNEL_BRANCH} to GitHub..."
        git -C "${A7A_KERNEL_DIR}" push origin "${A5E_KERNEL_BRANCH}" "${A7A_KERNEL_BRANCH}"
        echo "✅ Kernel pushed successfully!"
        ;;
    pull)
        require_branch "${A5E_KERNEL_DIR}" "${A5E_KERNEL_BRANCH}"
        require_branch "${A7A_KERNEL_DIR}" "${A7A_KERNEL_BRANCH}"
        echo "==> Pulling ${A5E_KERNEL_BRANCH} and ${A7A_KERNEL_BRANCH} from GitHub..."
        git -C "${A5E_KERNEL_DIR}" pull --ff-only origin "${A5E_KERNEL_BRANCH}"
        git -C "${A7A_KERNEL_DIR}" pull --ff-only origin "${A7A_KERNEL_BRANCH}"
        echo "==> Rebuilding kernel in bld.a5e..."
        if [ -d "${WORKSPACE_ROOT}/bld.a5e" ]; then
            make -C "${WORKSPACE_ROOT}/bld.a5e" linux-rebuild
        fi
        if [ -d "${WORKSPACE_ROOT}/bld.a7a" ]; then
            echo "==> Rebuilding kernel in bld.a7a..."
            make -C "${WORKSPACE_ROOT}/bld.a7a" linux-rebuild
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
        echo " [2] a7a kernel: linux-cubie (${A7A_KERNEL_BRANCH}):"
        echo "======================================================================"
        show_tree "${A7A_KERNEL_DIR}" "${A7A_KERNEL_BRANCH}"
        echo ""
        echo "======================================================================"
        echo " [3] a5e kernel: linux-v4-71 (${A5E_KERNEL_BRANCH}):"
        echo "======================================================================"
        show_tree "${A5E_KERNEL_DIR}" "${A5E_KERNEL_BRANCH}"
        echo "======================================================================"
        ;;
    *)
        usage
        ;;
esac
