#!/usr/bin/env bash
# Ciclo di validazione di un modulo kernel, in ordine di costo crescente.
#
# Sostituisce la sequenza che altrimenti si riscrive a mano ogni volta, e che a
# mano si accorcia sempre nello stesso punto: si compila, si vede che linka, e
# si salta checkpatch. Qui i passi ci sono tutti e il verdetto e' esplicito.
#
# NON carica il modulo: `insmod` su un kernel di sviluppo e' una decisione
# separata, da prendere in QEMU. Vedi SKILL.md.
#
# Uso:  kcheck.sh [directory-del-modulo]

set -uo pipefail

DIR="${1:-$PWD}"
KDIR="${KDIR:-/lib/modules/$(uname -r)/build}"
CHECKPATCH="$KDIR/scripts/checkpatch.pl"

cd "$DIR" 2>/dev/null || { echo "directory non valida: $DIR" >&2; exit 2; }

FAIL=0
step() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }
ok()   { printf '  \033[32mOK\033[0m      %s\n' "$1"; }
bad()  { printf '  \033[31mFALLITO\033[0m %s\n' "$1"; FAIL=1; }
warn() { printf '  \033[33mAVVISO\033[0m  %s\n' "$1"; }

SRC=$(ls ./*.c 2>/dev/null)
[[ -z "$SRC" ]] && { echo "nessun sorgente .c in $DIR" >&2; exit 2; }

# Un modulo va compilato con lo stesso compilatore del kernel. Dove il kernel e'
# costruito con clang, invocare make senza LLVM=1 fa fallire la build su opzioni
# che gcc non conosce (-mllvm, -mretpoline-external-thunk...). Non si assume
# nulla: il compilatore si legge da CONFIG_CC_IS_CLANG nel .config.
MAKEFLAGS_KBUILD=()
COMPILER="gcc di sistema"
if [[ -f "$KDIR/.config" ]] && grep -q '^CONFIG_CC_IS_CLANG=y' "$KDIR/.config"; then
    MAKEFLAGS_KBUILD+=(LLVM=1)
    COMPILER="clang (LLVM=1, come il kernel)"
fi

kbuild() { make -C "$KDIR" M="$PWD" "${MAKEFLAGS_KBUILD[@]}" "$@"; }

echo "=============================================================="
echo " VALIDAZIONE: $DIR"
echo " kernel:      $(uname -r)"
echo " compilatore: $COMPILER"
echo "=============================================================="

# ---------------------------------------------------------------- 1. build W=1
step "1. Build con W=1"
if [[ ! -f Makefile ]]; then
    bad "Makefile assente"
else
    BUILD_LOG=$(mktemp)
    if kbuild W=1 modules >"$BUILD_LOG" 2>&1; then
        WARNS=$(grep -cE 'warning:|error:' "$BUILD_LOG")
        if (( WARNS == 0 )); then
            ok "compilato senza warning"
        else
            warn "$WARNS warning:"
            grep -E 'warning:|error:' "$BUILD_LOG" | head -15 | sed 's/^/      /'
        fi
    else
        bad "compilazione fallita:"
        grep -E 'error:|Error' "$BUILD_LOG" | head -15 | sed 's/^/      /'
    fi
    rm -f "$BUILD_LOG"
fi

# ------------------------------------------------------------------ 2. checkpatch
step "2. checkpatch --strict"
if [[ -x "$CHECKPATCH" ]]; then
    CP_LOG=$(mktemp)
    # --no-tree: stiamo controllando un modulo out-of-tree, non un albero kernel.
    "$CHECKPATCH" --strict --no-tree --terse -f $SRC >"$CP_LOG" 2>&1
    ERRS=$(grep -c ':ERROR:' "$CP_LOG")
    WRNS=$(grep -c ':WARNING:' "$CP_LOG")
    CHKS=$(grep -c ':CHECK:'  "$CP_LOG")
    if (( ERRS == 0 && WRNS == 0 && CHKS == 0 )); then
        ok "nessun rilievo"
    else
        (( ERRS > 0 )) && bad "$ERRS errori"
        (( WRNS > 0 )) && warn "$WRNS warning"
        (( CHKS > 0 )) && warn "$CHKS check (stile stretto)"
        grep -E ':(ERROR|WARNING|CHECK):' "$CP_LOG" | head -20 | sed 's/^/      /'
    fi
    rm -f "$CP_LOG"
else
    warn "checkpatch non trovato in $CHECKPATCH"
fi

# ------------------------------------------------------------- 3. analisi statica
step "3. Analisi statica"
if command -v sparse >/dev/null 2>&1; then
    SP_LOG=$(mktemp)
    kbuild C=2 CF="-D__CHECK_ENDIAN__" modules >"$SP_LOG" 2>&1
    SP=$(grep -cE 'warning:|error:' "$SP_LOG")
    (( SP == 0 )) && ok "sparse pulito" || { warn "sparse: $SP rilievi"; grep -E 'warning:|error:' "$SP_LOG" | head -10 | sed 's/^/      /'; }
    rm -f "$SP_LOG"
else
    warn "sparse non installato — verifica annotazioni __user/__iomem saltata"
fi
command -v smatch >/dev/null 2>&1 || warn "smatch non installato — analisi di flusso saltata"
if ! command -v sparse >/dev/null 2>&1 || ! command -v smatch >/dev/null 2>&1; then
    # Il comando di installazione dipende dalla distribuzione: lo risolve doctor.sh.
    warn "per il comando adatto a questo sistema: binary-recon/scripts/doctor.sh"
fi

# --------------------------------------------------------------- 4. il modulo
step "4. Modulo prodotto"
KO=$(ls ./*.ko 2>/dev/null | head -1)
if [[ -z "$KO" ]]; then
    bad "nessun .ko prodotto"
else
    ok "$KO ($(stat -c '%s' "$KO") bytes)"
    modinfo "$KO" 2>/dev/null | awk -F: '/^(license|description|vermagic|depends):/ {
        key=$1; sub(/^[^:]*: */,"",$0); printf "      %-12s %s\n", key, $0 }'

    LIC=$(modinfo -F license "$KO" 2>/dev/null)
    [[ -z "$LIC" ]] && bad "MODULE_LICENSE assente: il modulo marcherebbe il kernel come tainted"

    ALIASES=$(modinfo "$KO" 2>/dev/null | grep -c '^alias:')
    if (( ALIASES > 0 )); then
        ok "$ALIASES alias: il caricamento automatico funziona"
    else
        warn "nessun alias — MODULE_DEVICE_TABLE dimenticato? Il modulo non si caricherà da solo"
    fi

    # Errori che sfuggono al compilatore ma si vedono nei simboli.
    if nm "$KO" 2>/dev/null | grep -qE ' U (__floatdi|__muldf|__divdf|__adddf)'; then
        bad "riferimenti a floating point: vietati in kernel space"
    fi
    if nm "$KO" 2>/dev/null | grep -qE ' U (msleep|schedule)$' && \
       nm "$KO" 2>/dev/null | grep -qE ' U _raw_spin_lock'; then
        warn "coesistono spinlock e funzioni che dormono: verifica di non dormire in sezione atomica"
    fi
fi

# ------------------------------------------------------------------ 5. verdetto
step "Verdetto"
if (( FAIL )); then
    printf '  \033[31mNON PRONTO\033[0m — risolvi i punti FALLITO qui sopra.\n'
else
    printf '  \033[32mPRONTO PER IL TEST\033[0m — build e controlli statici superati.\n'
fi
cat <<'NOTE'

  Restano da fare, e non li fa questo script:
    - test in QEMU (mai insmod sul kernel dell'host)
    - forzare ogni percorso di errore di probe()
    - load/unload ripetuti con kmemleak e lockdep attivi
    - rimozione durante l'uso, e ciclo suspend/resume
NOTE
exit $FAIL
