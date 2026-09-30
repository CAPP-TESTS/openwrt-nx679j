#!/usr/bin/env bash
# Inventario della toolchain disponibile su QUESTA macchina.
#
# Il profilo gira su host diversi, con distribuzioni e kernel diversi. Scrivere
# l'elenco degli strumenti dentro SOUL.md lo renderebbe falso ovunque tranne che
# sulla macchina dove e' stato scritto — e un agente che ragiona su un inventario
# sbagliato propone comandi che non esistono.
#
# Nulla qui dentro assume una distribuzione: gestore pacchetti, verbo di
# installazione e NOMI DEI PACCHETTI sono risolti a runtime. Lo stesso strumento
# si chiama in modi diversi altrove: `pahole` e' `dwarves` su Debian e Fedora,
# `dtc` e' `device-tree-compiler`, `perf` e' `linux-perf`.
#
# Esegui questo all'inizio di una sessione, o quando un comando manca.
#
# Uso:  doctor.sh [--brief]

set -uo pipefail
BRIEF=0
[[ "${1:-}" == "--brief" ]] && BRIEF=1

# ---------------------------------------------------------------- distribuzione
FAMILY="sconosciuta"
DISTRO_NAME="sconosciuta"
if [[ -r /etc/os-release ]]; then
    . /etc/os-release
    DISTRO_NAME="${PRETTY_NAME:-${NAME:-${ID:-sconosciuta}}}"
    for id in "${ID:-}" ${ID_LIKE:-}; do
        case "$id" in
            arch|archlinux|cachyos|manjaro|endeavouros) FAMILY=arch;   break ;;
            debian|ubuntu|linuxmint|pop|raspbian)       FAMILY=debian; break ;;
            fedora|rhel|centos|rocky|almalinux)         FAMILY=fedora; break ;;
            opensuse*|suse|sles)                        FAMILY=suse;   break ;;
            alpine)                                     FAMILY=alpine; break ;;
            gentoo)                                     FAMILY=gentoo; break ;;
        esac
    done
fi

# Il gestore effettivamente installato vince sulla famiglia dichiarata.
PKG="" INSTALL=""
for p in pacman apt dnf yum zypper apk xbps-install emerge; do
    command -v "$p" >/dev/null 2>&1 || continue
    PKG="$p"
    case "$p" in
        pacman)       INSTALL="sudo pacman -S --needed"; [[ $FAMILY == sconosciuta ]] && FAMILY=arch ;;
        apt)          INSTALL="sudo apt install";        [[ $FAMILY == sconosciuta ]] && FAMILY=debian ;;
        dnf|yum)      INSTALL="sudo $p install";         [[ $FAMILY == sconosciuta ]] && FAMILY=fedora ;;
        zypper)       INSTALL="sudo zypper install";     [[ $FAMILY == sconosciuta ]] && FAMILY=suse ;;
        apk)          INSTALL="sudo apk add";            [[ $FAMILY == sconosciuta ]] && FAMILY=alpine ;;
        xbps-install) INSTALL="sudo xbps-install -S" ;;
        emerge)       INSTALL="sudo emerge -a";          [[ $FAMILY == sconosciuta ]] && FAMILY=gentoo ;;
    esac
    break
done

# Helper AUR: esiste solo nel mondo Arch.
HELPER=""
for a in paru yay; do command -v "$a" >/dev/null 2>&1 && { HELPER="$a -S"; break; }; done

# ------------------------------------------------------------- nomi pacchetto
# tool|arch|debian|fedora|suse|alpine|gentoo
# Campo vuoto = non impacchettato per quella famiglia.
# AUR: richiede helper Arch. PIP: installabile via pip ovunque.
PKG_TABLE="
readelf|binutils|binutils|binutils|binutils|binutils|sys-devel/binutils
objdump|binutils|binutils|binutils|binutils|binutils|sys-devel/binutils
nm|binutils|binutils|binutils|binutils|binutils|sys-devel/binutils
strings|binutils|binutils|binutils|binutils|binutils|sys-devel/binutils
modinfo|kmod|kmod|kmod|kmod|kmod|sys-apps/kmod
file|file|file|file|file|file|sys-apps/file
gcc|gcc|build-essential|gcc|gcc|build-base|sys-devel/gcc
clang|clang|clang|clang|clang|clang|sys-devel/clang
make|make|make|make|make|make|sys-devel/make
gdb|gdb|gdb|gdb|gdb|gdb|dev-debug/gdb
pahole|pahole|dwarves|dwarves|dwarves|dwarves|dev-util/dwarves
dtc|dtc|device-tree-compiler|dtc|dtc|dtc|sys-apps/dtc
perf|perf|linux-perf|perf|perf|perf|dev-util/perf
bpftrace|bpftrace|bpftrace|bpftrace|bpftrace|bpftrace|dev-util/bpftrace
strace|strace|strace|strace|strace|strace|dev-debug/strace
ltrace|ltrace|ltrace|ltrace|ltrace||dev-debug/ltrace
sparse|sparse|sparse|sparse|sparse||dev-util/sparse
smatch|AUR:smatch|smatch|smatch|||dev-util/smatch
radare2|radare2|radare2|radare2|radare2|radare2|dev-util/radare2
rizin|rizin|rizin|rizin|||dev-util/rizin
binwalk|binwalk|binwalk|binwalk|binwalk||app-forensics/binwalk
qemu-system-x86_64|qemu-system-x86|qemu-system-x86|qemu-system-x86|qemu-x86|qemu-system-x86_64|app-emulation/qemu
vng|AUR:virtme-ng|PIP:virtme-ng|PIP:virtme-ng|PIP:virtme-ng|PIP:virtme-ng|PIP:virtme-ng
kernel-headers|linux-headers|linux-headers-\$(uname -r)|kernel-devel|kernel-devel|linux-headers|sys-kernel/linux-headers
"

pkg_for() {  # pkg_for <tool> -> comando di installazione, o stringa vuota
    local tool="$1" row field name
    row=$(awk -F'|' -v t="$tool" '$1 == t {print; exit}' <<<"$PKG_TABLE")
    if [[ -z "$row" ]]; then
        [[ -n "$INSTALL" ]] && echo "$INSTALL $tool"
        return
    fi
    case "$FAMILY" in
        arch) field=2 ;; debian) field=3 ;; fedora) field=4 ;;
        suse) field=5 ;; alpine) field=6 ;; gentoo) field=7 ;;
        *)    field=0 ;;
    esac
    if (( field == 0 )); then
        [[ -n "$INSTALL" ]] && echo "$INSTALL $tool"
        return
    fi
    name=$(cut -d'|' -f"$field" <<<"$row")
    [[ -z "$name" ]] && { echo "non impacchettato per $FAMILY — compila dai sorgenti"; return; }
    case "$name" in
        AUR:*) if [[ -n "$HELPER" ]]; then echo "$HELPER ${name#AUR:}"
               else echo "AUR: ${name#AUR:} — serve un helper (paru/yay) o build manuale"; fi ;;
        PIP:*) echo "pipx install ${name#PIP:}   (o pip install --user ${name#PIP:})" ;;
        *)     [[ -n "$INSTALL" ]] && echo "$INSTALL $name" || echo "installa il pacchetto '$name'" ;;
    esac
}

MISSING=()
check() {
    if command -v "$1" >/dev/null 2>&1; then
        (( BRIEF )) || printf '  \033[32m✓\033[0m %-20s %s\n' "$1" "$(command -v "$1")"
    else
        (( BRIEF )) || printf '  \033[31m✗\033[0m %-20s manca\n' "$1"
        MISSING+=("$1")
    fi
}
section() { (( BRIEF )) || printf '\n\033[1m%s\033[0m\n' "$1"; }

# ------------------------------------------------------------------- rapporto
echo "=============================================================="
echo " AMBIENTE kernel-re"
echo "=============================================================="
printf '  %-20s %s\n' "host" "$(uname -n)"
printf '  %-20s %s\n' "kernel" "$(uname -r)"
printf '  %-20s %s\n' "arch" "$(uname -m)"
printf '  %-20s %s\n' "distribuzione" "$DISTRO_NAME"
printf '  %-20s %s\n' "famiglia" "$FAMILY"
printf '  %-20s %s\n' "gestore pacchetti" "${PKG:-nessuno rilevato}${HELPER:+  (+${HELPER%% *})}"

section "Build kernel"
KBUILD="/lib/modules/$(uname -r)/build"
[[ -d "$KBUILD" ]] || KBUILD="/usr/src/linux-headers-$(uname -r)"
if [[ -d "$KBUILD" ]]; then
    printf '  \033[32m✓\033[0m %-20s %s\n' "header kernel" "$(readlink -f "$KBUILD")"
    for s in scripts/checkpatch.pl scripts/decode_stacktrace.sh .config; do
        if [[ -e "$KBUILD/$s" ]]; then
            printf '  \033[32m✓\033[0m %-20s presente\n' "$(basename "$s")"
        else
            printf '  \033[33m!\033[0m %-20s assente nel build tree\n' "$(basename "$s")"
        fi
    done
    if [[ -f "$KBUILD/.config" ]] && grep -q '^CONFIG_CC_IS_CLANG=y' "$KBUILD/.config"; then
        printf '  \033[33m!\033[0m %-20s kernel con clang: compila i moduli con LLVM=1\n' "compilatore"
    fi
else
    printf '  \033[31m✗\033[0m %-20s assenti — impossibile compilare moduli\n' "header kernel"
    MISSING+=("kernel-headers")
fi
for t in gcc clang make; do check "$t"; done

section "Analisi binaria"
for t in readelf objdump nm strings file modinfo radare2 rizin binwalk pahole dtc; do check "$t"; done

section "Debug e analisi dinamica"
for t in gdb strace ltrace perf bpftrace; do check "$t"; done

section "Analisi statica del codice kernel"
for t in sparse smatch; do check "$t"; done

section "Test isolato"
for t in qemu-system-x86_64 vng; do check "$t"; done

echo
echo "=============================================================="
if (( ${#MISSING[@]} == 0 )); then
    echo " Toolchain completa."
else
    echo " Mancano ${#MISSING[@]} strumenti. Impatto sul lavoro:"
    for tool in "${MISSING[@]}"; do
        case "$tool" in
          sparse)             why="niente verifica delle annotazioni __user/__iomem" ;;
          smatch)             why="niente analisi di flusso: null deref, off-by-one" ;;
          strace|ltrace)      why="niente tracciamento delle chiamate in userspace" ;;
          perf|bpftrace)      why="niente profiling ne' tracciamento dinamico" ;;
          radare2|rizin)      why="disassemblato interattivo non disponibile" ;;
          binwalk)            why="impossibile mappare ed estrarre immagini firmware" ;;
          pahole)             why="niente ispezione del layout delle struct" ;;
          dtc)                why="niente validazione dei Device Tree" ;;
          vng)                why="ciclo di test in QEMU manuale invece che immediato" ;;
          qemu-system-x86_64) why="NIENTE TEST ISOLATO: caricare moduli sull'host e' rischioso" ;;
          kernel-headers)     why="impossibile compilare qualunque modulo" ;;
          gdb)                why="niente debugging del kernel via QEMU" ;;
          *)                  why="" ;;
        esac
        printf '   \033[1m%-20s\033[0m %s\n' "$tool" "$why"
        cmd=$(pkg_for "$tool")
        [[ -n "$cmd" ]] && printf '   %-20s → %s\n' "" "$cmd"
    done
    [[ -z "$PKG" ]] && {
        echo
        echo " Nessun gestore pacchetti riconosciuto: installa gli strumenti con"
        echo " il metodo previsto da questo sistema."
    }
fi
echo "=============================================================="
