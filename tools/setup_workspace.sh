#!/usr/bin/env bash
# ==============================================================================
# setup_workspace.sh
# Multi-PC Workspace Setup and Git Synchronization Script for Cubie Development
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Repository URLs
REPO_CUBIE_A5E="git@github.com:tcmichals/cubie-a5e.git"
REPO_LINUX_CUBIE="git@github.com:tcmichals/linux-cubie.git"
REPO_BUILDROOT="https://gitlab.com/buildroot.org/buildroot.git"
# Each board has its own kernel tree and branch (.agents/rules/kernel-branch-discipline.md).
# linux-v4-71 is a git worktree of linux-cubie.
A5E_KERNEL_TREE="linux-v4-71"
A5E_KERNEL_BRANCH="cubie-linux-7.1"
A7A_KERNEL_TREE="linux-cubie"
A7A_KERNEL_BRANCH="a7a_remoteproc_camera_usb"

echo "======================================================================"
echo " Cubie Multi-Machine Workspace Setup"
echo " Workspace Root: ${WORKSPACE_ROOT}"
echo "======================================================================"

cd "${WORKSPACE_ROOT}"

# 0. Initialize or Update Git Submodules in cubie-a5e
echo ""
echo "[0/4] Initializing Git submodules in cubie-a5e..."
if [ -d "${WORKSPACE_ROOT}/cubie-a5e/.git" ] || [ -f "${WORKSPACE_ROOT}/cubie-a5e/.git" ]; then
    (
        cd "${WORKSPACE_ROOT}/cubie-a5e"
        git submodule update --init --recursive
    )
fi

# 1. Clone or Update linux-cubie
echo ""
echo "[1/4] Checking linux-cubie repository..."
if [ ! -d "${A7A_KERNEL_TREE}/.git" ]; then
    echo "  -> Cloning ${A7A_KERNEL_TREE} (${A7A_KERNEL_BRANCH}) from GitHub..."
    git clone -b "${A7A_KERNEL_BRANCH}" "${REPO_LINUX_CUBIE}" "${A7A_KERNEL_TREE}"
else
    echo "  -> ${A7A_KERNEL_TREE} already present. Fetching latest from GitHub..."
    git -C "${A7A_KERNEL_TREE}" fetch origin "${A7A_KERNEL_BRANCH}" "${A5E_KERNEL_BRANCH}" || true
    # Never switch branches here: the tree may hold upstream or in-flight work.
    current="$(git -C "${A7A_KERNEL_TREE}" branch --show-current)"
    if [ "${current}" != "${A7A_KERNEL_BRANCH}" ]; then
        echo "  -> WARNING: ${A7A_KERNEL_TREE} is on '${current}'; bld.a7a needs ${A7A_KERNEL_BRANCH}."
    fi
fi
if [ ! -e "${A5E_KERNEL_TREE}/.git" ]; then
    echo "  -> Adding worktree ${A5E_KERNEL_TREE} (${A5E_KERNEL_BRANCH})..."
    git -C "${A7A_KERNEL_TREE}" fetch origin "${A5E_KERNEL_BRANCH}:${A5E_KERNEL_BRANCH}" || true
    git -C "${A7A_KERNEL_TREE}" worktree add "../${A5E_KERNEL_TREE}" "${A5E_KERNEL_BRANCH}"
fi

# 2. Clone or Update Buildroot
echo ""
echo "[2/4] Checking buildroot repository..."
if [ ! -d "buildroot/.git" ]; then
    echo "  -> Cloning Buildroot from GitLab..."
    git clone "${REPO_BUILDROOT}" buildroot
else
    echo "  -> Buildroot already present."
fi

# 3. Configure bld.a5e
echo ""
echo "[3/4] Configuring bld.a5e build environment..."
mkdir -p bld.a5e
cat << 'EOF' > bld.a5e/local.mk
# Local Buildroot package overrides for live development
LINUX_OVERRIDE_SRCDIR = $(TOPDIR)/../linux-v4-71
EOF

if [ ! -f "bld.a5e/.config" ]; then
    echo "  -> Initializing bld.a5e with cubie_a5e_defconfig..."
    make -C buildroot O="${WORKSPACE_ROOT}/bld.a5e" BR2_EXTERNAL="${WORKSPACE_ROOT}/cubie-a5e/project-cubie-a5e" cubie_a5e_defconfig
else
    echo "  -> bld.a5e/.config exists. Updated local.mk."
fi

# 4. Configure bld.a7a
echo ""
echo "[4/4] Configuring bld.a7a build environment..."
mkdir -p bld.a7a
cat << 'EOF' > bld.a7a/local.mk
# Local Buildroot package overrides for live development
LINUX_OVERRIDE_SRCDIR = $(TOPDIR)/../linux-cubie
EOF

if [ ! -f "bld.a7a/.config" ]; then
    echo "  -> Initializing bld.a7a with cubie_a7a_defconfig..."
    make -C buildroot O="${WORKSPACE_ROOT}/bld.a7a" BR2_EXTERNAL="${WORKSPACE_ROOT}/cubie-a5e/project-cubie-a5e" cubie_a7a_defconfig
else
    echo "  -> bld.a7a/.config exists. Updated local.mk."
fi

# 5. Ensure Top-Level Workspace README exists
if [ -f "${SCRIPT_DIR}/WORKSPACE_README.md" ]; then
    cp "${SCRIPT_DIR}/WORKSPACE_README.md" "${WORKSPACE_ROOT}/README.md"
fi

# AI rules and prompts live only in cubie-a5e/.agents (open cubie-a5e as the
# workspace). They are not copied to the workspace root, so there is one copy.


echo ""
echo "======================================================================"
echo " Workspace Setup Complete!"
echo "======================================================================"
echo "Useful Commands:"
echo "  Build A5E Full Image:  make -C bld.a5e"
echo "  Rebuild Kernel Only:   make -C bld.a5e linux-rebuild"
echo "  Build A7A Full Image:  make -C bld.a7a"
echo "  Rebuild Kernel Only:   make -C bld.a7a linux-rebuild"
echo "  Push Kernel Commits:   cubie-a5e/tools/sync_kernel.sh push"
echo "  Pull Kernel Commits:   cubie-a5e/tools/sync_kernel.sh pull"
echo "======================================================================"
