#!/bin/sh
# Fetch embedded tool binaries for the target architecture and compress with zstd
# Usage: fetch_assets.sh <arch> [assets_dir]
#   arch: x86_64, aarch64, arm32v7, i386, riscv64, ppc64le
#   assets_dir: output directory (default: assets-<arch>)

set -e

ARCH="${1:-$(uname -m)}"
ASSETS_DIR="${2:-assets-${ARCH}}"

case "${ARCH}" in
    x86_64|amd64)    ARCH_ALT="x86_64";  DARCH="x86_64" ;;
    aarch64|arm64)   ARCH_ALT="aarch64"; DARCH="aarch64" ;;
    arm32v7|armv7l)  ARCH_ALT="arm32v7"; DARCH="arm" ;;
    i386|i686)       ARCH_ALT="i386";    DARCH="i386" ;;
    riscv64)         ARCH_ALT="riscv64"; DARCH="riscv64" ;;
    ppc64le)         ARCH_ALT="ppc64le"; DARCH="powerpc64le" ;;
    loongarch64)     ARCH_ALT="loongarch64"; DARCH="loongarch64" ;;
    s390x)           ARCH_ALT="s390x";   DARCH="s390x" ;;
    *) echo "Unknown arch: ${ARCH}"; exit 1 ;;
esac

SQUASHFUSE_VER="0.6.2.r0"
SQUASHFS_TOOLS_VER="4.7.5"
DWARFS_VER="0.15.3"

mkdir -p "${ASSETS_DIR}"

fetch_tool() {
    local name="$1"
    local url="$2"
    local out="${ASSETS_DIR}/${name}"

    if [ -f "${out}" ]; then
        echo "Already exists: ${out}"
        return 0
    fi

    echo "Fetching ${name} for ${ARCH_ALT}..."
    curl -L --insecure -o "${out}" "${url}" || {
        echo "Failed to fetch ${name}"
        return 1
    }
    chmod 755 "${out}"
}

compress_tool() {
    local name="$1"
    local src="${ASSETS_DIR}/${name}"
    local dst="${ASSETS_DIR}/${name}-zst"

    if [ -f "${dst}" ]; then
        echo "Already compressed: ${dst}"
        return 0
    fi

    echo "Compressing ${name} with zstd level 22..."
    zstd -22 --ultra -f -o "${dst}" "${src}" || {
        # Fallback to lower level
        zstd -19 -f -o "${dst}" "${src}"
    }
}

if ! command -v curl >/dev/null 2>&1; then
    echo "curl not found, cannot download assets"
    exit 1
fi
if ! command -v zstd >/dev/null 2>&1; then
    echo "zstd not found, cannot compress assets"
    exit 1
fi

fetch_tool "squashfuse" \
    "https://github.com/VHSgunzo/squashfuse-static/releases/download/v${SQUASHFUSE_VER}/squashfuse-musl-mimalloc-${ARCH_ALT}"

fetch_tool "unsquashfs" \
    "https://github.com/VHSgunzo/squashfs-tools-static/releases/download/v${SQUASHFS_TOOLS_VER}/unsquashfs-${ARCH_ALT}"

fetch_tool "mksquashfs" \
    "https://github.com/VHSgunzo/squashfs-tools-static/releases/download/v${SQUASHFS_TOOLS_VER}/mksquashfs-${ARCH_ALT}" || true

# DwarFS binary (UPX-compressed, needs upx -d)
DWARFS_UPX="dwarfs-universal.upx"
DWARFS_BIN="dwarfs-universal"
DWARFS_URL="https://github.com/mhx/dwarfs/releases/download/v${DWARFS_VER}/dwarfs-universal-${DWARFS_VER}-Linux-${DARCH}.upx"
fetch_tool "${DWARFS_UPX}" "${DWARFS_URL}" || true
if [ -f "${ASSETS_DIR}/${DWARFS_UPX}" ]; then
    if command -v upx >/dev/null 2>&1; then
        echo "Decompressing DwarFS binary with upx..."
        upx -d "${ASSETS_DIR}/${DWARFS_UPX}" -o "${ASSETS_DIR}/${DWARFS_BIN}" 2>/dev/null
        chmod 755 "${ASSETS_DIR}/${DWARFS_BIN}" 2>/dev/null || true
    fi
fi

# Download dwarfsextract binary (non-UPX) for extraction operations
DWARFSEXTRACT_URL="https://github.com/mhx/dwarfs/releases/download/v${DWARFS_VER}/dwarfs-fuse-extract-${DWARFS_VER}-Linux-${DARCH}"
fetch_tool "dwarfsextract" "${DWARFSEXTRACT_URL}" || true

compress_tool "squashfuse"
compress_tool "unsquashfs"
[ -f "${ASSETS_DIR}/${DWARFS_BIN}" ] && compress_tool "${DWARFS_BIN}" || \
    echo "DwarFS binary not available for this arch"
[ -f "${ASSETS_DIR}/dwarfsextract" ] && compress_tool "dwarfsextract" || \
    echo "dwarfsextract binary not available for this arch"
echo "Assets prepared in ${ASSETS_DIR}/"
echo "Creating symlink: assets -> ${ASSETS_DIR}"
ln -sfn "${ASSETS_DIR}" "assets" 2>/dev/null || true
