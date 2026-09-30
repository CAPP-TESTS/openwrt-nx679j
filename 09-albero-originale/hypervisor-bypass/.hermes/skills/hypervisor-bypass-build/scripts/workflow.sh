#!/usr/bin/env bash
set -euo pipefail

ROOT="${HYPERVISOR_BYPASS_ROOT:-$(pwd)}"
if [[ "${1:-}" == "--root" ]]; then
    [[ $# -ge 2 ]] || { printf '%s\n' 'workflow: --root requires a path' >&2; exit 2; }
    ROOT="$2"
    shift 2
fi

ACTION="${1:-check}"
BUILD_DIR="$ROOT/build"
EVIDENCE_DIR="$BUILD_DIR/evidence"
KDIR="${KDIR:-/home/user/nx679j-stock/kernel-patch-test/linux-6.6.60}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

die() {
    printf 'workflow: %s\n' "$*" >&2
    exit 1
}

need_command() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

check_project() {
    [[ -d "$ROOT" ]] || die "project root does not exist: $ROOT"
    [[ -f "$ROOT/Makefile" ]] || die "Makefile not found under $ROOT"
    [[ -f "$ROOT/kexec_injector.c" ]] || die "kexec_injector.c not found"
    [[ -f "$ROOT/hyp_attack.c" ]] || die "hyp_attack.c not found"
    [[ -d "$ROOT/magisk_module" ]] || die "magisk_module directory not found"
    [[ -d "$KDIR" ]] || die "kernel tree not found: $KDIR"
    [[ -f "$KDIR/Makefile" ]] || die "kernel Makefile not found: $KDIR/Makefile"
    need_command make
    need_command "${CROSS_COMPILE}gcc"
    need_command sha256sum
    need_command file
    printf 'ok: project=%s\n' "$ROOT"
    printf 'ok: kernel=%s\n' "$KDIR"
    printf 'ok: compiler=%s\n' "${CROSS_COMPILE}gcc"
    if [[ ! -f "$KDIR/.config" ]]; then
        printf 'warning: kernel .config is missing; module build may fail\n'
    fi
}

write_evidence() {
    mkdir -p "$EVIDENCE_DIR"
    {
        printf 'timestamp_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        printf 'project_root=%s\n' "$ROOT"
        printf 'kernel_tree=%s\n' "$KDIR"
        printf 'host=%s\n' "$(uname -a)"
        printf 'compiler='
        "${CROSS_COMPILE}gcc" --version | sed -n '1p'
        printf 'make='
        make --version | sed -n '1p'
        if [[ -d "$ROOT/.git" ]]; then
            printf 'git_revision='
            git -C "$ROOT" rev-parse HEAD
        else
            printf 'git_revision=unavailable\n'
        fi
    } > "$EVIDENCE_DIR/manifest.txt"

    {
        for path in \
            "$ROOT/Makefile" \
            "$ROOT/kexec_injector.c" \
            "$ROOT/hyp_attack.c" \
            "$ROOT/dtb_patch.dts" \
            "$ROOT/magisk_module/module.prop" \
            "$ROOT/build/hyp_attack" \
            "$ROOT/build/gunyah_watchdog_pet.zip" \
            "$ROOT/kexec_injector.ko"; do
            [[ -f "$path" ]] && sha256sum "$path"
        done
    } >> "$EVIDENCE_DIR/manifest.txt"
    printf 'ok: evidence=%s\n' "$EVIDENCE_DIR/manifest.txt"
}

case "$ACTION" in
    check)
        check_project
        ;;
    module)
        check_project
        make -C "$ROOT" KDIR="$KDIR" CROSS_COMPILE="$CROSS_COMPILE" module
        ;;
    userspace)
        check_project
        make -C "$ROOT" CROSS_COMPILE="$CROSS_COMPILE" userspace
        ;;
    build)
        check_project
        make -C "$ROOT" KDIR="$KDIR" CROSS_COMPILE="$CROSS_COMPILE" build-all
        write_evidence
        ;;
    package)
        check_project
        need_command zip
        make -C "$ROOT" CROSS_COMPILE="$CROSS_COMPILE" userspace
        mkdir -p "$BUILD_DIR"
        rm -f "$BUILD_DIR/gunyah_watchdog_pet.zip"
        (cd "$ROOT" && zip -qr "$BUILD_DIR/gunyah_watchdog_pet.zip" magisk_module)
        write_evidence
        printf 'ok: package=%s\n' "$BUILD_DIR/gunyah_watchdog_pet.zip"
        ;;
    evidence)
        check_project
        write_evidence
        ;;
    help|-h|--help)
        printf '%s\n' 'usage: workflow.sh [--root PATH] {check|module|userspace|build|package|evidence}'
        ;;
    *)
        printf 'workflow: unknown action: %s\n' "$ACTION" >&2
        exit 2
        ;;
esac
