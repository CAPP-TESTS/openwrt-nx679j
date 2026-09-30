# What the network does to an idle bearer: the timers, with citations

Read this before deciding *when* a bearer may have died and *what evidence* to expect. It answers the operator-side question: which timers end an idle PDN connection / PDU session, which of them are visible to the host, and what a UE must do to survive them.

All citations were read out of the specification versions below (downloaded from the 3GPP archive, text extracted from the `.docx`):

| Spec | Version cited | Used for |
|---|---|---|
| TS 23.401 | V19.6.0 | EPC session/bearer life, reachability timers, PGW-initiated deactivation |
| TS 24.301 | V19.8.0 | EMM/ESM timers, detach types, ESM causes |
| TS 23.060 | V19.0.0 | GPRS PDP context / SGSN-IMSI detach |
| TS 24.008 | V19.5.0 | GMM/SM timers (T3312, T3314, T3324), network-initiated PDP deactivation |
| TS 23.501 | V19.9.0 | 5GC RM states, MICO, power-saving enhancements |
| TS 24.501 | V19.8.0 | 5GMM/5GSM timers, deregistration/PDU session release |
| TS 23.502 | V20.3.0 | Network-initiated deregistration, network-requested PDU session release |
| TS 23.203 | V20.1.0 | PCC: the *only* standardized "inactivity timer" for an IP-CAN session |
| TS 23.682 | V19.3.0 | PSM / Active Time / high-latency buffering |
| TS 29.274 | V19.6.0 | GTPv2 causes (incl. #11) and the GTP→NAS cause mapping |

## 1. Four different endings — do not conflate them

| Ending | Signalling to the UE? | Context gone? | Where it is specified |
|---|---|---|---|
| RAN releases the connection after RAN-side **user inactivity** | no NAS message | **no** — ECM-/CM-IDLE keeps the PDN connection/PDU session | TS 23.401 §5.3.5 (S1 release, cause "User Inactivity" per TS 36.413) |
| Operator/PGW/MME/SMF **explicitly deactivates** bearer(s) or the whole PDN connection | yes — Delete Bearer/PDU Session Release Command or Detach/Deregistration Request, with a cause | yes | TS 23.401 §5.4.4.1, §5.10.3, §5.3.8.3; TS 24.301 §6.4.4; TS 24.501 §6.3.3 |
| Reachability failure → **implicit detach / implicit deregistration** | **none** (defined as "the MME does not send the Detach Request") | yes — every PDN connection / PDU session | TS 23.401 §4.3.5.2 + §5.3.8.3 step 1; TS 24.301 §5.3.5; TS 24.501 §5.3.7; TS 23.501 §5.3.2.2.3 |
| Idle/state timeout **outside 3GPP** (Gi/SGi NAT & firewall state timeout, IP-pool reclaim) | no | no (the NAS context is intact and still "registered") | not specified in 3GPP |

The last row is the one that produces a bearer that looks up but carries nothing while every NAS counter is healthy. It is indistinguishable from an explicit deactivation at the host unless you probe the data plane; it is *not* a reason to re-establish the session.

## 2. The reachability chain — the timers that end everything

Both RATs run the same three-stage chain, UE-side timer first:

1. **UE periodic timer** — expired, the UE must contact the network or lose everything.
2. **Network "mobile reachable" timer** — started by the MME/AMF/SGSN when the UE enters idle; expiry means "the UE is probably gone". The network does **not** delete the context here.
3. **Network "implicit detach / implicit deregistration" timer** — started at (2); expiry means "the UE is gone" and the context is deleted without any message to the UE.

| Timer | Who runs it | Value | Citation |
|---|---|---|---|
| **T3412** (periodic TAU) | UE (EPS) | Default **54 min**; the network may assign another value in the GPRS timer / GPRS timer 3 IE (TS 24.008 §10.5.7.3, §10.5.7.4a) | TS 24.301 §5.3.5, Table 10.2.1 |
| **T3512** (periodic registration) | UE (5GS) | Default **54 min**; not stopped on entering CM-CONNECTED if the NW indicates *strictly periodic registration timer* | TS 24.501 §5.3.7, Table 10.2.1 |
| **T3312** (periodic RAU) | MS (GPRS) | Default **54 min**, started when READY / PMM-CONNECTED is left, stopped on re-entering | TS 24.008 Table 11.3a |
| **Mobile reachable timer** | MME / AMF / SGSN | By default **4 minutes greater than T3412** (EPS); **4 min greater than T3512** (5GS); equal to T3412 **if the UE is attached for emergency services** | TS 24.301 §5.3.5, Table 10.2.2 NOTE 1; TS 24.501 §5.3.7 |
| **Implicit detach timer** | MME | "The value … is network dependent"; **default T3423 + 4 min if ISR is activated**; "relatively large value … at least slightly larger than the UE's E-UTRAN Deactivate ISR timer" | TS 24.301 §5.3.5; TS 23.401 §4.3.5.2 |
| **Implicit deregistration timer** | AMF | Network dependent; started when the mobile reachable timer expires; **entering 5GMM-IDLE with MICO + strictly periodic monitoring → started immediately** | TS 24.501 §5.3.7, Table 10.2.2 |
| **Mobile Reachable Timer** (legacy) | SGSN | Started when the MS enters STANDBY/IDLE; on expiry the SGSN "shall start the Implicit Detach timer and shall allow the MS to be paged" | TS 23.060 §6.2.3 |

The MME's own wording for stage 3, so the "stale because nobody noticed" failure is expected rather than surprising:

> "Instead the MME should clear the PPF flag in the MME and start an Implicit Detach timer, with a relatively large value … With the PPF clear, the MME does not page the UE in EUTRAN coverage and shall send a Downlink Data Notification Reject message to the Serving GW … If the Implicit Detach timer expires before the UE contacts the network, then the MME can deduce that the UE has been 'out of coverage' for a long period of time and implicitly detach the UE." — TS 23.401 §4.3.5.2

> "The MME initiated detach procedure is either explicit (e.g. by O&M intervention) or implicit. The MME may implicitly detach a UE, if it has not had communication with UE for a long period of time. The MME does not send the Detach Request (Detach Type) message to the UE for implicit detach." — TS 23.401 §5.3.8.3 step 1

Deleting the context is part of it: implicit detach deletes the GGSN/PGW PDP/bearer contexts (TS 23.060 §6.1.1: "The MM and PDP contexts in the SGSN may be deleted. The GGSN PDP contexts shall be deleted.") and the MME sends `Delete Session Request` per PDN connection (TS 23.401 §5.3.8.3 step 2). 5GS is the same shape: "The AMF shall enter RM-DEREGISTERED state for the UE after Implicit Deregistration" (TS 23.501 §5.3.2.2.3), and the AMF's implicit deregistration may simply stop paging (TS 24.501 §5.3.7).

**Operational consequence for a host-held bearer:** the worst case is `T3412/T3512 + mobile-reachable margin + implicit-detach timer`. The skill's rule "sample beyond the longest internal timer" therefore has a floor of roughly **T3412 + 4 min**, and the implicit-detach timer above it is *network-dependent and not signalled to the UE* — so the only hard bound you actually know is the one you can compute from the last accepted TAU/registration. Everything above it you must detect by probing, not by timing.

Emergency-attached exceptions (shorter, and locally enforced): the mobile reachable timer equals T3412 and the MME **locally detaches the UE** on expiry (TS 24.301 §5.3.5); the AMF does the same in 5GS (TS 24.501 §5.3.7). Implicit detach for emergency-attached UEs uses "an inactivity timeout specific to emergency" (TS 23.401 §5.3.8.3).

## 3. Explicit, operator-initiated deactivation (signalling *does* arrive)

### 3.1 EPC / GPRS

- **PGW-initiated bearer deactivation** — TS 23.401 §5.4.4.1. Trigger is policy/PCC or O&M, not an idle timer. The PGW sends `Delete Bearer Request`; step 1 of that clause is the only place in EPC where a *generic* inactivity trigger appears, and it is restricted to emergency/RLOS:
  > "For an emergency PDN connection the PDN GW initiates the deactivation of all bearers of that emergency PDN connection when the PDN connection is inactive (i.e. not transferring any packets) for a configured period of time…" — TS 23.401 §5.4.4.1
  RLOS: "The PDN GW initiates the deactivation of all bearers of that RLOS PDN connection … per local operator policy."
- **MME-requested PDN disconnection** — TS 23.401 §5.10.3 (also used when only non-emergency PDN connections may be dropped, TS 23.401 §5.3.8.3 step 2).
- **GGSN-initiated PDP context deactivation** — TS 23.060 §9.2.4.3, with the same emergency-inactivity wording restricted to emergency PDP contexts.
- **Network-initiated PDP context deactivation** — TS 24.008 §6.1.3.4.2: `DEACTIVATE PDP CONTEXT REQUEST` with cause **#8** Operator Determined Barring, **#25** LLC/SNDCP failure, **#26** insufficient resources, **#36** regular deactivation, **#38** network failure, **#39** reactivation requested, **#112**, **#113**.

### 3.2 The causes the UE must act on

The reaction is specified, not optional:

| Cause received | Meaning | UE obligation | Citation |
|---|---|---|---|
| ESM/5GSM **#39 "reactivation requested"** | the network wants the same APN/DNN back | "The UE should then re-activate the EPS bearer/PDN connection"; in 5GS: "the UE should re-initiate the UE-requested PDU session establishment" | TS 24.301 §6.4.4.3; TS 24.501 §6.3.3.3 |
| ESM **#36 "regular deactivation"** / **#38 "network failure"** | operator-side teardown / failure | deactivate locally, no automatic re-activation required | TS 24.301 §9.9.4.4 |
| 5GSM **#36 / #38** | same | same | TS 24.501 §9.11.4.2 |
| **last PDN connection** deactivated with reactivation requested | becomes a **detach with detach type "re-attach required"** | UE re-attaches from scratch | TS 29.274 Table C.3 (mapping #8 → #39 / "re-attach required"); TS 24.301 §5.5.2.3.2 |

The GTP→NAS mapping that makes the "reactivation requested" of a GTPv2 `Delete Bearer Request` show up as an ESM cause:

> "#8 'Reactivation Requested' … Shall be mapped to: #39 'Reactivation requested' in the NAS bearer context deactivation procedure. For the last PDN connection in E-UTRAN, 'Reactivation requested' shall be mapped to 're-attach required' in the NAS detach type IE." — TS 29.274 Table C.3 (§14.3.2)

UE-side mandatory action on detach type "re-attach required" (TS 24.301 §5.5.2.3.2):

> "the UE shall deactivate the EPS bearer context(s), if any, including the default EPS bearer context locally without peer-to-peer signalling between the UE and the MME. The UE shall stop the timer T3346, if it is running …"

5GS equivalent: DEREGISTRATION REQUEST with "re-registration required" → "the UE shall perform a local release of the PDU sessions over 3GPP access" (TS 24.501 §5.5.2.3.2); the UE also starts T3540 in that case (TS 24.501 §5.3.1.3).

**Impact on the host:** every one of these ends the modem's WDS/PDU session and invalidates the L3 — the next session usually lands on a different subnet, so re-establish **and** re-apply L3 (see the parent skill's renewal procedure). A "reactivation requested" is the one case where reconnecting immediately is the specified behaviour rather than an over-eager loop.

## 4. The inactivity timers that actually exist in the specs

There is exactly one standardized "inactivity timer" that terminates an IP-CAN session, it lives in PCC, and it is scoped to IMS emergency service:

> "at reception of an IP-CAN Session Modification Request … that removes all PCC rules with a QCI other than the default bearer QCI and the QCI used for IMS signalling, the PCEF shall start a configurable inactivity timer (e.g. to enable PSAP Callback session). When the configured period of time expires the PCEF shall initiate an IP-CAN Session Termination Request for the IP-CAN session serving the IMS Emergency session." — TS 23.203 §6.1.10.3.2 (PCEF)

…and it is cancelled if new non-default QoS rules arrive (same clause). For RLOS there is no inactivity timer at all — "Duration of PDN connection for RLOS is controlled through local policies in PCEF. Handling of inactivity timer for the emergency PDN connection is not applicable for RLOS." (TS 23.203 §6.1.10a).

How that reaches the UE, on S5/S8 only, is a dedicated GTPv2 cause:

> **#11 "PDN connection inactivity timer expires"** — used by the PGW to indicate that the inactivity timer for the emergency PDN connection has expired and all bearers are deleted, "as specified in TS 23.203". — TS 29.274 §7.2.9 (Delete Session Request / Delete Bearer Request) and Table 8.4-1

**Boundary condition to state plainly: for a normal, non-emergency PDN connection/PDU session, 3GPP defines no inactivity deactivation.** No NAS/NGAP message will arrive to announce an idle teardown unless the operator tears it down explicitly through §3 or the UE fails reachability through §2. Operator practice for reclaiming idle contexts is therefore either PCC/O&M-driven (visible, §3) or entirely outside the specs (Gi/SGi NAT + firewall state timeouts, DHCP pool reclaim) — invisible to NAS, §1 row 4.

## 5. What does *not* end the context

- **S1 release / NG release after RAN inactivity.** TS 23.401 §5.3.5 lists the eNodeB-initiated S1 release cause **"User Inactivity"** (TS 36.413) alongside "Detach", "Error Indication" etc. The UE goes to ECM-IDLE; the PDN connection and IP address are kept, and downlink data triggers paging/buffering (Downlink Data Notification). Treat a released RRC/S1 connection as *normal*, never as a bearer death.
- **GSM/UMTS READY-timer expiry**: the MS and SGSN MM contexts return to STANDBY (TS 24.008 §4.7.2.1, T3314 default 44 s); PDP contexts are untouched. (TS 23.060 §6.1.1)
- **ISR deactivation timers**, EMM-REGISTERED-without-PDN-connection, and Service Gap Control are registration/idle bookkeeping, not context deletion.

## 6. Reachability-reducing features — asking for shorter timers

These change *what the network expects* and thereby how fast §2 fires. Enable them deliberately, never by accident:

| Feature | Effect | Citation |
|---|---|---|
| **PSM** (Active Time / T3324, extended periodic TAU) | after Active Time expires the UE is **not reachable**; the network must buffer or notify via high-latency procedures. "A UE in PSM is not immediately reachable" | TS 23.682 §4.5.4; TS 24.008 Table 11.3a (T3324); TS 23.401 §4.3.5.2 (MME "active timer") |
| **eDRX** | UE not listening most of the time; same buffering machinery | TS 23.682 §4.5.4 (network chooses PSM, eDRX or both) |
| **MICO** | "the AMF considers the UE always unreachable while the UE CM state in the AMF is CM-IDLE"; AMF **rejects** downlink data delivery for the UE | TS 23.501 §5.4.1.3 |
| **MICO + strictly periodic registration timer / long periodic timer** | implicit deregistration timer can start as soon as the UE enters CM-IDLE with MICO | TS 23.501 §5.31.7.5; TS 24.501 §5.3.7 Table 10.2.2 |
| **High-latency communication** (extended buffering in the SGW/UPF, NEF "UE Reachability"/"Availability after DDN failure" notifications) | the correct alternative when MT traffic must still work | TS 23.682 §4.5.7; TS 23.501 §5.31.8; TS 23.401 §5.3.11 |

A device that must hold a bearer for hours *and* answer mobile-terminated traffic should not use PSM/MICO. If it does, "the bearer died" is frequently just Active Time expiring — the modem looks registered while the network has stopped paging.

## 7. The timers that block your recovery

An aggressive renewal loop hits these, and they are the reason a re-attach can fail for minutes while the modem reports the network as present:

| Timer | Value | Effect | Citation |
|---|---|---|---|
| **T3402** (EPS) / **T3502** (5GS) | Default **12 min** | after 5 failed attach/TAU or registration attempts the UE waits this long before retrying | TS 24.301 Table 10.2.1; TS 24.501 Table 10.2.1 |
| **T3346** (MM congestion / back-off) | network-provided | UE must not send MM requests | TS 24.301 §5.3.5, Tables 10.2.1/10.2.2 |
| **T3396** (session-management back-off) | network-provided | blocks requests for the same APN/DNN (stops on #39 reactivation requested, TS 24.008 §6.1.3.4.2 / TS 24.301 §5.5.2.3.2) | TS 24.301 Table 10.3.1 note; TS 24.501 §10.3 |
| **T3447 / T3448** | network-provided | extended-wait / service back-off after network-initiated detach scenarios | TS 24.301 Table 10.2.1 |

One protective interaction worth knowing: if the MME sends a T3346 larger than T3412, it must set the mobile reachable and implicit detach timers so that their **sum** exceeds T3346 (TS 24.301 §5.3.5) — i.e. the network will not implicitly detach a UE it has itself parked on a long back-off.

## 8. Resilience checklist for a host that must hold a bearer

1. **Know which timer value you were granted.** Read T3412/T3512 (and T3324) out of the last ACCEPT message and log them. Everything time-based about your supervisor hangs off these numbers; they are not constants (54 min is only a default).
2. **Never let the periodic update stop.** The radio must come back to send TAU/registration. Parking the radio, holding a stale PLMN, or a host-side power policy that keeps the modem from transmitting silently converts a bearer into an implicit detach at `T3412 + 4 min` at the earliest.
3. **Sample inside the shortest, decide on the longest.** Your detection budget must be shorter than `T3412 + 4 min` if you want to see the drop, and your measurement campaign must exceed the network-dependent implicit-detach timer, which you cannot read — so prove continuity by probing, not by extrapolating from the granted value.
4. **Treat every ending as "session + L3".** On ESM/5GSM #39, #36, #38, detach "re-attach required" or deregistration "re-registration required", re-establish the WDS session/PDU session and re-apply address+route: the new PDN has a new subnet (TS 29.274 Table C.3; TS 24.301 §5.5.2.3.2).
5. **Implement the specified reaction to #39.** It is the operator asking for immediate reconnection; reconnecting at once is correct here and is not an infinite loop, because the cause is one-shot.
6. **Do not model the invisible case as a session end.** A Gi/SGi state timeout leaves the NAS context alive: keep the session, prove the path with a data-plane probe, and if you need to survive it, keep traffic inside the operator's idle window (or use a periodic probe) — do not tear down and re-dial, which costs a full attach and may land you on a back-off timer.
7. **Honour back-off timers yourself.** T3346/T3396/T3402/T3502/T3447 govern the *network's* willingness to serve you; a retry loop that ignores them gets you longer deregistration, not faster recovery.
8. **Bind recovery to the modem's own indication.** The host's view of these events is the WDS/packet-service-status callback and its call-end reason; there is no NAS message for the implicit case, and the kernel's L3 is stale in every case (see the parent skill's ownership-model and probe references).
9. **Do not use a network-initiated-deactivation test as a liveness test.** The only way to observe §2's implicit detach end-to-end is to let a bearer go unreachable for `T3412 + 4 min` and beyond, and confirm that the host detects it and that the modem's session is actually gone — plan for hours, not minutes.

## 9. Assumptions that are wrong

- *"There is a PDP/EPS inactivity timer the subscriber can read and set."* Only the emergency/IMS case exists (TS 23.203 §6.1.10.3.2) and it is enforced in the PCEF/PGW; the UE is never told the value.
- *"The network tells you before it releases an idle context."* Implicit detach/deregistration is explicitly defined to be *without* a message (TS 23.401 §5.3.8.3; TS 23.502 §4.2.2.3.3).
- *"The S1/NG connection was released, so the bearer is gone."* S1 release for "User Inactivity" is routine and preserves the PDN connection (TS 23.401 §5.3.5).
- *"Registered means reachable."* After the mobile reachable timer the network stops paging and answers DDN with a reject (TS 23.401 §4.3.5.2; TS 24.501 §5.3.7).
- *"If the bearer is up, the NAS context is up."* False in the other direction too: a Gi/SGi state timeout breaks the path with every NAS counter intact (§1).
- *"A round number of seconds means the modem dropped it."* For this class of failures the round number is a timer — T3412+4 min, a network implicit-detach value, an Active Time, or an operator NAT idle window. Attribute it only after reading the granted timers and the modem's call-end reason.

## 10. Where each number came from

Clause list for re-verification (body text, not tables of contents): TS 23.401 §4.3.5.2, §4.3.17.3, §5.3.5, §5.3.8.3, §5.4.4.1, §5.10.3; TS 24.301 §5.3.5, §5.5.2.3.2, §6.4.4.2/§6.4.4.3, §9.9.3.7, §9.9.4.4, Tables 10.2.1/10.2.2/10.3.1; TS 23.060 §6.1.1, §6.2.3, §6.6, §9.2.4.3; TS 24.008 §4.7.2.1, §6.1.3.4.2, Tables 11.3/11.3a; TS 23.501 §5.3.2.2.3, §5.4.1.3, §5.31.7.5, §5.31.8; TS 24.501 §5.3.7, §5.5.2.3.2, §6.3.3.2/§6.3.3.3, §9.11.4.2, Tables 10.2.1/10.2.2/10.3.1; TS 23.502 §4.2.2.3.3, §4.3.4; TS 23.203 §6.1.10.3.2, §6.1.10a; TS 23.682 §4.5.4, §4.5.7; TS 29.274 §7.2.9, Table 8.4-1, Tables C.2/C.3 (§14.3.2).
