#!/bin/bash
# setup-npu-bundle.sh: Automate the retrieval and setup of proprietary NPU libraries.
set -e

WORKSPACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUNDLE_DIR="${WORKSPACE_DIR}/timvx-bundle"
BUNDLE_LIB_DIR="${BUNDLE_DIR}/lib"
BUNDLE_BIN_DIR="${BUNDLE_DIR}/bin"

# The target NPU userspace libraries required for TIM-VX / OpenVX acceleration
NPU_LIBS=(
    "libtim-vx.so"
    "libGAL.so"
    "libVSC.so"
    "libArchModelSw.so"
    "libNNArchInfo.so"
    "libVIPhal.so"
    "libvx_delegate.so"
)

NPU_BINS=(
    "vpm_run"
)

show_help() {
    echo "Usage: $0 [method] [options]"
    echo ""
    echo "Methods:"
    echo "  ssh [ip] [user]      Fetch NPU libraries from a running board via SSH"
    echo "  image [path_to_img]  Extract NPU libraries from a local Radxa Debian/Ubuntu .img file"
    echo "  help                 Show this help menu"
    echo ""
}

setup_directories() {
    echo "Creating bundle directories in ${BUNDLE_DIR}..."
    mkdir -p "${BUNDLE_LIB_DIR}"
    mkdir -p "${BUNDLE_BIN_DIR}"
}

fetch_via_ssh() {
    local ip="${1[:-192.168.1.100]}" # default placeholder ip
    local user="${2:-rock}"
    
    if [ -z "$1" ]; then
        read -p "Enter Cubie A5E IP address [192.168.1.100]: " ip
        ip="${ip:-192.168.1.100}"
        read -p "Enter username [rock]: " user
        user="${user:-rock}"
    fi

    echo "Attempting to fetch NPU libraries from ${user}@${ip}..."
    setup_directories

    # Pull libraries
    for lib in "${NPU_LIBS[@]}"; do
        echo "Fetching ${lib}..."
        # Try /usr/lib and /usr/lib/aarch64-linux-gnu
        scp "${user}@${ip}:/usr/lib/${lib}*" "${BUNDLE_LIB_DIR}/" 2>/dev/null || \
        scp "${user}@${ip}:/usr/lib/aarch64-linux-gnu/${lib}*" "${BUNDLE_LIB_DIR}/" 2>/dev/null || \
        echo "--> Warning: Could not find ${lib} on remote board (might be optional or named differently)."
    done

    # Pull test binaries (optional)
    for bin in "${NPU_BINS[@]}"; do
        echo "Fetching binary ${bin}..."
        scp "${user}@${ip}:/usr/bin/${bin}" "${BUNDLE_BIN_DIR}/" 2>/dev/null || \
        echo "--> Warning: Could not find binary ${bin} on remote board."
    done

    echo "Done! NPU libraries are populated in: ${BUNDLE_DIR}"
}

extract_from_image() {
    local img_path="$1"
    
    if [ -z "${img_path}" ]; then
        read -p "Enter path to Radxa Debian .img file: " img_path
    fi

    if [ ! -f "${img_path}" ]; then
        echo "Error: Image file '${img_path}' not found."
        exit 1
    fi

    setup_directories

    echo "Mounting image using loop device..."
    # Find partition offset for the rootfs partition (usually partition 2 or 3)
    # We look for the Linux/ext4 partition
    local part_info=$(fdisk -l "${img_path}" | grep -E "img[p-z]?[0-9]" | grep -i "Linux" || true)
    if [ -z "${part_info}" ]; then
        echo "Error: Could not locate rootfs partition offset in image."
        exit 1
    fi

    local start_sector=$(echo "${part_info}" | awk '{print $2}')
    local offset=$((start_sector * 512))

    local mount_point=$(mktemp -d)
    echo "Mounting rootfs partition at ${mount_point} (requires sudo)..."
    sudo mount -o loop,offset=${offset},ro "${img_path}" "${mount_point}"

    echo "Copying libraries..."
    # Copy NPU libraries
    for lib in "${NPU_LIBS[@]}"; do
        local found=0
        if [ -f "${mount_point}/usr/lib/${lib}" ]; then
            cp -d "${mount_point}/usr/lib/${lib}"* "${BUNDLE_LIB_DIR}/"
            found=1
        elif [ -f "${mount_point}/usr/lib/aarch64-linux-gnu/${lib}" ]; then
            cp -d "${mount_point}/usr/lib/aarch64-linux-gnu/${lib}"* "${BUNDLE_LIB_DIR}/"
            found=1
        fi
        if [ ${found} -eq 0 ]; then
            echo "--> Warning: Could not find ${lib} in the image."
        else
            echo "Copied ${lib}."
        fi
    done

    # Copy NPU test binaries
    for bin in "${NPU_BINS[@]}"; do
        if [ -f "${mount_point}/usr/bin/${bin}" ]; then
            cp "${mount_point}/usr/bin/${bin}" "${BUNDLE_BIN_DIR}/"
            echo "Copied binary ${bin}."
        fi
    done

    echo "Unmounting image..."
    sudo umount "${mount_point}"
    rmdir "${mount_point}"

    echo "Done! NPU libraries are populated in: ${BUNDLE_DIR}"
}

# Main routing
case "$1" in
    ssh)
        fetch_via_ssh "$2" "$3"
        ;;
    image)
        extract_from_image "$2"
        ;;
    help|*)
        show_help
        ;;
esac
