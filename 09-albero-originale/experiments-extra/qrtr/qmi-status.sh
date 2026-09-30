#!/bin/sh
# qmi-status.sh - minimal, read-only QMI status sampler for NX679J (X65) over QRTR.
#
# One sample per cycle; pass an interval in seconds to loop.
# Read-only by construction: it only ever sends the four Get messages below.
# It never starts/stops a bearer, binds a mux, or touches DMS/WDA/DPM setters.
#
# Design + rationale: experiments/qrtr/QMI-STATUS-READER-DESIGN.md
#
# Usage:  qmi-status.sh            # one sample, print, exit
#         qmi-status.sh 30         # sample every 30 s until killed

DEV="${QMI_DEV:-qrtr://0}"
QMI="${QMI_BIN:-/usr/bin/qmicli}"
TMO="${QMI_CALL_TIMEOUT:-4}"      # hard per-call ceiling, seconds
BACKOFF_MAX="${QMI_BACKOFF_MAX:-300}"

# --- qmicli with a hard timeout (this image has no coreutils `timeout`) ------
# The watchdog's stdout/stderr MUST be redirected: in the callers the output is
# captured with $(...), and command substitution does not return until every
# writer of the pipe closes it -- a watchdog that inherited the fd would stall
# each call for the full TMO.
q() {
	"$QMI" -d "$DEV" "$@" 2>&1 &
	p=$!
	( sleep "$TMO"; kill "$p" 2>/dev/null ) >/dev/null 2>&1 &
	w=$!
	wait "$p"; rc=$?
	kill "$w" 2>/dev/null
	return $rc
}

# first value of "Key: 'value'" in $2
fld() { printf '%s\n' "$2" | sed -n "s/.*$1: *'\([^']*\)'.*/\1/p" | head -n 1; }
# first value of a "[n]: 'value'" list entry
lst() { printf '%s\n' "$1" | sed -n "s/.*\[0\]: *'\([^']*\)'.*/\1/p" | head -n 1; }

# --- one cycle ---------------------------------------------------------------
sample() {
	nas_srv=$(q --nas-get-serving-system)   || true
	nas_sig=$(q --nas-get-signal-info)       || true
	wds_sts=$(q --wds-get-packet-service-status) || true
	dms_opm=$(q --dms-get-operating-mode)    || true

	reg=$(fld  "Registration state" "$nas_srv")
	rat=$(lst  "$nas_srv")
	mcc=$(fld  "MCC" "$nas_srv")
	mnc=$(fld  "MNC" "$nas_srv")
	op=$(fld   "Description" "$nas_srv")
	rsrp=$(fld "RSRP" "$nas_sig")
	rsrq=$(fld "RSRQ" "$nas_sig")
	snr=$(fld  "SNR"  "$nas_sig")
	data=$(fld "Connection status" "$wds_sts")
	mode=$(fld "Mode" "$dms_opm")

	# A field that is absent is "unknown", not an error. Only a total miss of
	# every service is a failed cycle.
	ok=0; [ -n "$mode" ] && ok=1; [ -n "$reg" ] && ok=1
	ts=$(awk '{printf "%.2f", $1}' /proc/uptime)

	# QMI protocol errors that mean a *state*, not a fault (see design doc 6.2):
	#   WDS 0x0024 -> error 70 InvalidOperation ; WDS 0x002D -> error 15 OutOfCall
	case "$wds_sts" in
		*"InvalidOperation"*|*"OutOfCall"*) data="down" ;;
	esac

	printf 'ts=%s reg=%s rat=%s mcc=%s mnc=%s op=%s rsrp=%s rsrq=%s snr=%s data=%s mode=%s ok=%d\n' \
		"${ts:--}" "${reg:--}" "${rat:--}" "${mcc:--}" "${mnc:--}" "${op:--}" \
		"${rsrp:--}" "${rsrq:--}" "${snr:--}" "${data:--}" "${mode:--}" "$ok"
	return $((1 - ok))
}

# --- main --------------------------------------------------------------------
interval="${1:-}"
fails=0
backoff=0

while :; do
	sample || fails=$((fails + 1))

	[ -z "$interval" ] && break

	if [ "$fails" -eq 0 ]; then
		backoff=0
		sleep "$interval"
	else
		# exponential backoff on consecutive failures, capped
		backoff=$((backoff * 2))
		[ "$backoff" -lt "$interval" ] && backoff="$interval"
		[ "$backoff" -gt "$BACKOFF_MAX" ] && backoff="$BACKOFF_MAX"
		sleep "$backoff"
	fi
done
