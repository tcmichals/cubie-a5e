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
KERNEL_BRANCH="cubie-linux-7.1"

echo "======================================================================"
echo " Cubie Multi-Machine Workspace Setup"
echo " Workspace Root: ${WORKSPACE_ROOT}"
echo "======================================================================"

cd "${WORKSPACE_ROOT}"

# 1. Clone or Update linux-cubie
echo ""
echo "[1/4] Checking linux-cubie repository..."
if [ ! -d "linux-cubie/.git" ]; then
    echo "  -> Cloning linux-cubie (${KERNEL_BRANCH}) from GitHub..."
    git clone -b "${KERNEL_BRANCH}" "${REPO_LINUX_CUBIE}" linux-cubie
else
    echo "  -> linux-cubie already present. Fetching latest from GitHub..."
    (
        cd linux-cubie
        git fetch origin "${KERNEL_BRANCH}" || true
        echo "  -> Current branch: $(git branch --show-current)"
    )
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
LINUX_OVERRIDE_SRCDIR = $(TOPDIR)/../linux-cubie
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

# 6. Synchronize .agents rules and prompt guidelines to workspace root
if [ -d "${SCRIPT_DIR}/../.agents" ]; then
    echo ""
    echo "[5/5] Synchronizing .agents rules to workspace root..."
    mkdir -p "${WORKSPACE_ROOT}/.agents"
    cp -r "${SCRIPT_DIR}/../.agents/"* "${WORKSPACE_ROOT}/.agents/"
fi


echo ""
echo "======================================================================"
echo " Workspace Setup Complete!"
echo "======================================================================"
echo "Useful Commands:"
echo "  Build A5E Full Image:  make -C bld.a5e"
echo "  Rebuild Kernel Only:   make -C bld.a5e linux-rebuild"
echo "  Build A7A Full Image:  make -C bld.a7a"
echo "  Rebuild Kernel Only:   make -C bld.a7a linux-rebuild"
echo "  Push Kernel Commits:   git -C linux-cubie push origin ${KERNEL_BRANCH}"
echo "  Pull Kernel Commits:   git -C linux-cubie pull origin ${KERNEL_BRANCH}"
echo "======================================================================"
