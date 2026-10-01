.. SPDX-License-Identifier: GPL-2.0

=========================
XGS-PON technology primer
=========================

Scope
=====

A passive optical network, or PON, is a point to multipoint optical
access network. One fiber leaves the operator's equipment, a passive
splitter divides the light and each branch ends at a subscriber.
Nothing between the two ends is powered. The operator end is the optical
line termination, or OLT. The subscriber end is the optical network
unit, or ONU. The fiber and splitters between them are the optical
distribution network, or ODN.

ITU-T G.9807.1 (02/2023) defines the 10-Gigabit-capable symmetric PON,
XGS-PON: the optics in Annex B, the transmission convergence layer in
Annex C, activation in clause C.12, security in clause C.15. ITU-T G.988
(11/2022) defines the ONU management and control interface, OMCI: the
managed entities in clause 9, the message set in clause 11, the
procedures in Annex B and Appendix I. Clause references below are to
G.9807.1 unless the text names G.988.

This document covers the optical layer, the framing, the grant
mechanism, the encapsulation, the security, the activation protocol and
the management interface. Every number, field width, identifier and code
point carries the clause that states it. It does not cover the optical
component specifications of Annex B, the reach extender cases, the
wavelength overlay, or the multi-wavelength systems that G.9807.1 refers
to but does not define.

Acronyms and terms
==================

Network entities
----------------

- **OLT**: optical line termination, the operator end. **ONU**: optical
  network unit, the subscriber end. **ONT**: optical network
  termination, an ONU serving one subscriber.
- **ODN**: optical distribution network, the fiber and the splitters.
  **PON**: passive optical network.

The Recommendations and their systems
-------------------------------------

- **XGS-PON**: the symmetric 10-Gigabit-capable PON of G.9807.1.
  **XG-PON**: the asymmetric one it coexists with.
- **GPON**: the gigabit PON of the G.984 series, written G-PON there.
  **EPON**: Ethernet passive optical network, a different technology
  described at the end.

Transmission convergence
------------------------

- **TC**: transmission convergence. **XGS TC**: the TC layer of XGS-PON.
  **XGTC**: that of XG-PON. **GTC**: that of GPON. G.988 manages "GTC
  based PON systems".
- **PHY**: physical layer, whose units are the PHY frame and PHY burst.
  **FS**: framing sublayer, whose units are the FS frame and FS burst.
  **PSBd**, **PSBu**: the downstream and upstream physical
  synchronization blocks.
- **FEC**: forward error correction, here a Reed-Solomon code. **BCH**:
  the code family in the header checks. **HEC**: hybrid error
  correction, a BCH code plus one parity bit.
- **BIP**: bit-interleaved parity. **CRC**: cyclic redundancy check.
  **SFC**: superframe counter. **BER**: bit error ratio.

Media access
------------

- **TDM**: time division multiplexing, downstream. **TDMA**: time
  division multiple access, upstream.
- **BWmap**: bandwidth map, the upstream schedule sent downstream.
  **allocation structure**: one entry of it. **grant**: one upstream
  transmission opportunity, a bandwidth allocation in clause 3.1.4.
- **Alloc-ID**: allocation identifier, the recipient of a grant.
  **T-CONT**: transmission container, the ONU object that takes grants.
- **DBA**: dynamic bandwidth assignment. **SR-DBA**: status reporting
  DBA. **TM-DBA**: traffic-monitoring DBA. **DBRu**: dynamic bandwidth
  report upstream.
- **quiet window**: an interval with no grants to the ONUs in service.
  **guard time**: the gap between adjacent bursts. **equalization
  delay**: the per-ONU delay that aligns the upstream.

The data plane
--------------

- **GEM**: the encapsulation method of the GPON family. **XGEM**: that
  of XGS-PON. **Port-ID**: XGEM port identifier, the flow label.
- **SDU**: service data unit, usually an Ethernet frame. **PLI**:
  payload length indication. **LF bit**: last fragment bit.
- **idle frame**: a XGEM frame on the idle Port-ID. **key index**: the
  header field naming the encryption key. **MPLS**: multi-protocol
  label switching.

Control and activation
----------------------

- **PLOAM**: physical layer operations, administration and maintenance.
  **OAM**: operations, administration and maintenance. The embedded OAM
  channel is a set of header fields, not a message channel.
- **ONU-ID**: the identifier the OLT assigns. **serial number**: a
  Vendor_ID and a vendor-specific serial number. **registration id**:
  the Registration_ID string held by the ONU.
- **MIC**: message integrity check. **FWI**: forced wake-up indication,
  a BWmap flag. **O1 to O7**: the activation cycle states. **TO1**,
  **TO2**: two of their timers.
- **OC**: the operation control structure of the PSBd. **LSB**: least
  significant bit. **ASCII**: the character encoding of that name.

Security
--------

- **AES**: advanced encryption standard. **CMAC**: cipher-based message
  authentication code. **CTR**: counter mode. **ECB**: electronic
  codebook mode.
- **MSK**: master session key. **SK**: session key. **KEK**: key
  encryption key. **OMCI_IK**, **PLOAM_IK**: the two integrity keys.
  **SN**: the ONU serial number, as the key formulas write it.

Management
----------

- **OMCI**: ONU management and control interface, the G.988 protocol.
  **OMCC**: its channel, a XGEM port. **ME**: managed entity, the unit
  of the object model. **MIB**: the ME instances in an ONU.
- **MIB data sync**: the sequence number that audits the MIB. **AVC**:
  attribute value change, an autonomous notification. **PM**:
  performance monitoring. **TCA**: threshold crossing alert.
- **ANI**: access node interface, the PON-facing side. **UNI**: user
  network interface, the subscriber-facing side.
- **PPTP**: physical path termination point. **CTP**: connection
  termination point. **VEIP**: virtual Ethernet interface point.
  **GAL**: the adaptation layer named by the GAL Ethernet profile
  managed entity (G.988 clause 9.2.7).
- **MT**: the OMCI message type field. **TCI**: transaction correlation
  identifier. **AR**: acknowledge request bit. **AK**: acknowledgment
  bit.
- **MAC**: medium access control. **LAN**: local area network. **VLAN**:
  virtual local area network. **WRR**: weighted round robin. **IEEE**:
  the body whose 802.1 standards G.988 references.

Deployment
----------

The next four terms name ONU shapes that the industry uses. G.9807.1
lists SBU and the pair MDU/SFU in its abbreviation list and G.988 lists
MDU, but no clause of either Recommendation defines them as categories
and HGU appears in neither.

- **SFU**: single-family unit, an ONU serving one dwelling over
  Ethernet. **HGU**: home gateway unit, an ONU that also routes.
- **SBU**: small business unit, an ONU for a business site. **MDU**:
  multi-dwelling unit, an ONU serving several subscribers.

Optics
------

- **SFP**: small form-factor pluggable, an industry form factor.
  **BOSA**: bidirectional optical sub-assembly, an industry term for
  transmitter, receiver and filter in one package.
- **APD**: avalanche photodiode, the usual detector, an industry term.
  **TIA**: transimpedance amplifier, the stage after it.
- **burst mode**: the OLT receiver behavior each new burst demands.
  **OLS**: optical layer supervision (clause B.II.1). **DDM**: digital
  diagnostic monitoring, the industry name for its transceiver half.
- **RSSI**: received signal strength indication (clause C.12.1.3).
  **WDM**: wavelength division multiplexing. **WDM1r**: the filter that
  puts two systems on one ODN.

Defects and alarms
------------------

- **LOS**: loss of signal, at the OLT. **LOBi**: loss of burst for
  ONU i. **LODS**: loss of downstream synchronization, at the ONU.
- **LOPCi**, **LOOCi**: loss of the PLOAM and OMCC channels with ONU i.
  **SUFi**: start-up failure of ONU i. **DFi**: disable failure of
  ONU i.
- **TIW**: transmission interference warning. **DOWi**: drift of window
  for ONU i. **dying gasp**: the ONU's imminent power loss warning.
- **signal fail**, **signal degrade**: the ANI-G alarms on two
  downstream BER thresholds.
- **LOF**, **DACT**, **DIS**, **MIS**, **PEE**, **RDI**: older defect
  names from the earlier generation, which this document does not
  define. See the section on defects.

The network
===========

One fiber leaves the OLT port, a splitter or a cascade of splitters
divides the optical power and each output reaches one ONU. Clause A.8.5
sets a 1:64 split as the minimum requirement (so that the system can
coexist with the gigabit systems already on the plant) and records that
the TDMA control function should support a 256-way or larger logical
split, while the physical split must be chosen with the maturity and the
cost of the optical devices in mind. Clause A.8.6
requires at least 20 km of fiber, then requires the TC layer to match
XG-PON (which starts at 60 km) and to support up to 40 km of maximum
differential fiber distance in 20 km steps. Differential distance, the
difference between the nearest and furthest ONU on one ODN, sets how far
apart in time two upstream bursts can drift. Table B.6.2 names two
categories, DD20 and DD40.

Table B.6.1 gives the optical path loss classes. Clause B.6.1 makes the
minimum a requirement too: an ODN with less loss must carry extra
attenuators, to avoid damage to receivers.

=====  ============  ============
Class  Minimum loss  Maximum loss
=====  ============  ============
B+     13 dB         28 dB
C+     17 dB         32 dB
N1     14 dB         29 dB
N2     16 dB         31 dB
E1     18 dB         33 dB
E2     20 dB         35 dB
=====  ============  ============

Clause A.8.2 defines two wavelength sets. The basic set reuses the
XG-PON waveband: 1260 to 1280 nm upstream, 1575 to 1580 nm downstream,
extended to 1581 nm outdoors. The optional set reuses the GPON waveband,
1300 to 1320 nm upstream and 1480 to 1500 nm downstream and the clause
gives its reason directly: 1300 to 1320 nm is compatible with all
deployed WDM1r devices. An operator cannot remove a working gigabit
system to install a new one, so both must share the plant, in different
wavebands, combined by a passive filter. Clause A.8.4 ties the basic set
to N1 at 29 dB and the optional set to B+ at 28 dB, after the WDM1r
loss. Clause A.5.2.3 states the cost of each: the basic set makes one
port carry XGS-PON and XG-PON ONUs together, with dual rate TDMA
upstream and TDM downstream, while the optional set separates them by
wavelength. Clause A.6 lists four coexistence scenarios and clause
3.2.3 calls the device in the middle a coexistence element, connecting
PON systems of different Recommendation series to the same ODN.

The two directions differ in access method. Downstream the OLT transmits
continuously and every ONU receives everything (clause C.10.1.1). Clause
C.6.1.5.1 states that the OLT multiplexes XGEM frames using the Port-ID
as the key and that each ONU filters on that key, so a frame for one
subscriber physically reaches all of them and privacy depends on
encryption, not on topology. Upstream the ONUs share the fiber in time:
clause C.10.1.2 states that each ONU transmits short PHY bursts and
stays silent between them with its transmitter disabled and that the
OLT uses the BWmap to control the timing and duration of every burst so
that bursts do not overlap. An ONU is dark unless the OLT told it to
transmit. That asymmetry sets the hardware: the ONU needs a transmitter
it can switch on and off inside a burst gap and the OLT needs a
receiver that recovers level and clock from a new burst at a new power
and phase thousands of times a second and a scheduler exact in time,
because a late burst lands on another subscriber's burst.

The optical layer at the ONU
============================

Clause B.9.2.1 gives the line rate as 9.95328 Gbit/s in each direction
and states that the rate is a multiple of 8 kHz, which makes the
125 microsecond frame an exact number of bytes. Table C.6.1 lets an OLT
support 9.95328 Gbit/s downstream with 2.48832 Gbit/s upstream, with
9.95328 Gbit/s upstream, or with both at once, the last being a dual
rate OLT carrying both kinds of ONU in one PHY frame. Table C.6.2 is
shorter: an XGS-PON ONU runs 9.95328 Gbit/s both ways and clause
C.6.1.1 makes 2.48832 Gbit/s upstream optional for it.

Clause C.10.1.2 states the transmitter rule plainly: the ONU disables
the transmitter between bursts. Appendix B.III says what the gap buys,
naming laser on and off time, timing drift tolerance, level recovery,
clock recovery and start of burst delimitation and divides the overhead
into a guard time, a preamble time and a delimiter time.

========================  ==========  ==========
Interval (Table B.III.2)  Worst case  Objective
========================  ==========  ==========
Transmitter enable        1 280 bits  256 bits
Transmitter disable       1 280 bits  256 bits
Guard time                2 048 bits  512 bits
Preamble time             6 080 bits  1 280 bits
Delimiter time            64 bits     32 bits
Total overhead            8 192 bits  2 048 bits
========================  ==========  ==========

Table B.11.1 gives the upstream physical layer overhead at
9.95328 Gbit/s as 2 048 bits, the objective total and clause C.10.1.2.3
recommends a minimum guard time of 512 bits, covering the transmitter
enable and disable times plus margin for the drift of the individual
ONU. Appendix B.III states that the split is partly constraint and
partly choice: the next burst's laser ramp must not fall on the previous
burst's data, the previous burst's tail must not fall on the next
burst's preamble and within those the OLT implementer chooses, because
the OLT receiver has to cope. It requires a delimiter of at least 16
bits at a BER of 1e-4, notes that burst mode receiver designs differ in
speed and in the preamble pattern they prefer and warns that the
patterns must stay balanced and keep a reasonable transition density or
the ONU transmitter circuitry suffers. Appendix C.III recommends a 1 280
bit preamble and a 32 bit delimiter, or 64 bits under a higher BER and
Table C.III.1 suggests the preamble patterns 0xBB521E26 and 0xAAAAAAAA,
each with matching delimiters and distinct delimiters for FEC on and
FEC off.

Both ends measure their own optics. Appendix B.II calls this optical
layer supervision, leaves the method to the implementer and lists seven
items: transceiver temperature, transceiver voltage and laser bias
current at both ends, transmit power at both ends, receive power at the
ONU and receive power per ONU at the OLT. Table B.II.1 gives optical
power a typical resolution of 0.1 dB and a typical response time of
300 ns, with accuracy of plus or minus 2 dB at the OLT and 3 dB at the
ONU and its Note 3 states the burst mode complication, that the OLT
measures average power during a burst and must time the measurement
against it. G.9807.1 defines no message for reporting this and clause
C.14 hands the job to G.988, whose clause 9.2.1 puts an optical signal
level attribute and a transmit optical level attribute on the ANI-G,
both two's complement integers referred to 1 mW with 0.002 dB
granularity, with thresholds in 0.5 dB steps and matching alarms. The
industry calls the transceiver side of this digital diagnostic
monitoring. That name is not ITU-T text. Clause C.12.1.3 adds one
measurement no attribute exposes: the OLT monitors the received signal
strength indication, the phase and the BER of each upstream burst.

Dying gasp is a one-bit warning at bit 0 of the Ind field of the
upstream FS burst header (clause C.8.1.2.1.2). The clause is careful:
the bit says the ONU found a local condition that may stop it answering
allocations, it does not commit the ONU to stopping, the ONU clears it
if the condition passes and the OLT should not withdraw allocations on
the bit alone. Its stated value is that it separates a plant fault from
a premises event. G.988 clause 9.1.1 carries the event upward as ONU-G
alarm 7 and clause C.14.2.3 adds an urgent ONU status snapshot record,
written during the dying gasp sequence and whenever ONU software
switches the transmitter off, held in non-volatile memory that survives
reactivation and power loss and should hold at least ten records.

G.9807.1 defines the rogue ONU by exactly one behavior. Clause C.19.1
states it: an ONU transmits in the wrong upstream time slot and
interferes with another ONU's transmission and the OLT detects LOBi. It
lists no causes and specifies no mitigation protocol. Clause C.12.1.5
lists the tools, equalization delay readjustment, ONU-ID deactivation
and serial number disabling and adds that if the offender never
declared a serial number, the OLT may disable every ONU and re-enable
the conformant ones one at a time. Clause C.19.4 requires the OLT to
accept a signal received during a quiet window only if the PSBu
structure is valid and clause C.19.5 describes an idle window, in which
the OLT withholds every allocation, as a way to find a transmitter that
is on the fiber but does not speak the protocol.

Downstream framing
==================

Clause C.10.1.1 fixes the downstream PHY frame at 125 microseconds,
155 520 bytes at 9.95328 Gbit/s: a 24-byte PSBd followed by the PHY
frame payload. The start of a frame is defined per network element, as
the moment the OLT transmits or the ONU receives the first bit of PSBd.

Clause C.10.1.1.1 divides the PSBd into three 8-byte structures. PSync
is the fixed 64-bit pattern 0xC5E51840FD59BB49 (clause C.10.1.1.1.1).
The superframe counter structure holds a 51-bit counter and a 13-bit
HEC (clause C.10.1.1.1.2). The counter increments once per frame and
wraps from all ones to zero. The operation control structure holds a
51-bit body and a 13-bit HEC (clause C.10.1.1.1.3). Clause C.10.1.1.2
adds a detail that catches implementers: after the HEC is computed and
before it is verified, both structures are exclusive-ORed with the fixed
pattern 0x0F0F0F0F0F0F0F0F. The operation control body carries the
channel's static identity:

- an 8-bit PON-ID type, split into a reach extender flag, a 3-bit ODN
  class, a downstream FEC flag, a protocol flag and a 2-bit link type
- a 32-bit PON-ID, split into a 28-bit administrative label and a 4-bit
  downstream wavelength channel identifier
- two single-bit fields named R and C
- a 9-bit transmit optical level in 0.1 dB steps referred to -30 dBm,
  with 0x1FF meaning not supported

For XGS-PON the downstream FEC flag and the protocol flag must both be 1
and R and C must both be 0.

Clause C.10.1.1.3 gives a reference synchronization state machine, Hunt,
Pre-Sync, Sync and Re-Sync and states that the real mechanism inside
the ONU is not standardized. In Hunt the ONU searches every bit and byte
alignment for an exact PSync match, then checks that the next 64 bits
form a valid superframe counter structure. Once locked, verification
runs once per frame boundary: PSync passes if at least 62 of 64 bits
match and the counter passes if the structure is valid and the received
value equals the locally incremented copy. In Re-Sync one successful
verification returns the ONU to Sync, while M - 1 consecutive failures
make it declare loss of downstream synchronization, discard its counter
copy and return to Hunt. The recommended value of M is 3.

The PHY frame payload is 155 496 bytes (clause C.10.1.1.4), obtained
from the FS frame by FEC encoding and then scrambling. Clause
C.10.1.3.1.1 gives the downstream code as RS(248,216) with 627 codewords
per PHY frame and excludes the PSBd, so the first codeword starts at
byte 25 and the codewords carry 627 times 216, that is 135 432 data
bytes, exactly the FS frame size of clause C.8.1.1. Clause C.10.1.4.1
gives the scrambler polynomial as x^58 + x^39 + 1, reset at the first
bit after the PSBd, with a 58-bit preload made of the 51-bit superframe
counter and seven trailing ones.

::

    downstream PHY frame, 125 microseconds, 155 520 bytes

    +------------------------+-------------------------------------+
    |      PSBd, 24 B        |   PHY frame payload, 155 496 B      |
    |  (not FEC protected)   |   627 x RS(248,216), then scrambled |
    +------------------------+-------------------------------------+
    | PSync | SFC   | OC     |
    |  8 B  | 8 B   | 8 B    |
    +-------+-------+--------+

    the 627 x 216 = 135 432 data bytes are the FS frame:

    +-------+---------------+-----------------+-----------+--------+
    | HLend | BWmap         | PLOAMd          | FS        | FS     |
    |  4 B  | 8 x N bytes   | 48 x P bytes    | payload   |trailer |
    +-------+---------------+-----------------+-----------+--------+
    \------------ FS frame header ------------/            4 bytes

    HLend, 4 bytes:

    +----------------------+---------------+----------------------+
    |  BWmap length, 11 b  | PLOAM count,  |     HEC, 13 bits     |
    |  (= N)               |   8 bits (=P) |                      |
    +----------------------+---------------+----------------------+

Clause C.8.1.1.1 defines HLend: the BWmap length field is the number of
allocation structures N, the PLOAM count field the number of PLOAM
messages P and the HEC a truncated BCH(63,12,2) code over the first 31
bits plus one parity bit. The BWmap partition is 8 times N bytes and the
PLOAMd partition 48 times P bytes, since every PLOAM message is 48 bytes
(clauses C.8.1.1.2 and C.8.1.1.4). Clause C.9.1.1 gives the payload size
only as a formula, the FS frame size less the header and the trailer.
Clause C.8.1.1.5 defines the trailer as 4 bytes whose contents are at
the discretion of the OLT, states that the ONU takes its link BER from
the FEC correction results because downstream FEC is statically on and
adds one coexistence rule that the section on GPON and XG-PON returns
to: the OLT avoids
ending the FS payload with a short idle XGEM frame.

Upstream bursts
===============

Clause C.10.1.2 fixes the upstream PHY frame at the same
125 microseconds and 155 520 bytes. Its boundary is not an observable
event: each ONU derives it by offsetting its own downstream frame
boundary and clause C.8.1.1.2.3 notes that the OLT and each ONU
associate the start of an upstream frame with generally different
moments in time.

Clause C.10.1.2.1 defines the PSBu as a preamble and a delimiter, whose
lengths and patterns are the burst profile. The OLT publishes the valid
profiles in advance with Burst_Profile PLOAM messages, each carrying a
distinct index and the BWmap selects one per burst through a 2-bit
BurstProfile field (clause C.8.1.1.2.6). Table C.11.4 gives the profile
contents: a 4-bit version, a rate applicability bit, a 2-bit index, an
upstream FEC flag, a delimiter length of 0 to 8 octets with its pattern,
a preamble length of 1 to 8 octets with a repeat count and its pattern
and an 8-byte PON-TAG. Clause C.11.3.3.1 adds two rules that matter for
activation: profile information does not survive an activation cycle
and an ONU may answer an allocation only after acquiring the profile
that allocation names.

::

    upstream PHY burst

    +----------------+----------------------------------------------+
    |   PSBu         | PHY burst payload: FS burst, optional FEC,    |
    | preamble +     | then scrambled                                |
    | delimiter      |                                               |
    +----------------+----------------------------------------------+
      (not in a FEC codeword)

    the FS burst:

    +---------+----------+-------+----------+-------+---------+-------+
    | FS hdr  | PLOAMu   | DBRu  | payload  | DBRu  | payload | FS    |
    |  4 B    | 0 or 48 B|  4 B  | alloc 1  |  4 B  | alloc 2 |trailer|
    +---------+----------+-------+----------+-------+---------+-------+
     \-- FS burst header --/      \- one allocation interval -/   4 B

    fixed FS burst header, 4 bytes:

    +----------------+-----------------+----------------------------+
    |  ONU-ID, 10 b  |   Ind, 9 bits   |        HEC, 13 bits        |
    +----------------+-----------------+----------------------------+

Clause C.8.1.2.1 splits the FS burst header into a 4-byte fixed section,
the ONU-ID, the Ind field and the HEC and a non-fixed section that is
either nothing or one 48-byte PLOAM message. The OLT chooses with the
PLOAMu flag of the first allocation structure, making the header 52
bytes instead of 4. Clause C.8.1.2.1.3 builds that HEC from a truncated
BCH(63,12,2) code over the 31 initial bits of the header plus a parity
bit (as HLend does), not over 63 bits as the allocation structure and
the XGEM header do. The ONU-ID field lets the OLT confirm that the
expected ONU is transmitting. An ONU with no ONU-ID yet sends 0x03FF
(clause C.8.1.2.1.1). In the Ind field, bit 8 is the PLOAM queue status,
set when messages remain pending after this burst, bits 7 to 1 are
reserved and bit 0 is the dying gasp bit. The field is the fast
unsolicited status path from the ONU, costing no message and no grant
(clause C.8.1.2.1.2).

If the DBRu flag is set, the allocation interval begins with the 4-byte
DBRu structure, whose first 3 bytes are the buffer occupancy in 4-byte
units aggregated over all buffers of that Alloc-ID, with 0x000000
meaning empty and 0xFFFFFF an invalid measurement, protected by a CRC-8
using x^8 + x^2 + x + 1 (clauses C.8.1.2.2.1 and C.8.1.2.2.2). Unlike
ITU-T I.432.1, which uses the same polynomial, the result is not
exclusive-ORed with 0x55 and the receiver corrects what the code allows
and discards the report only on an uncorrectable error. Clause
C.8.1.2.3 defines the trailer as a 4-byte bit-interleaved even parity
field over the whole FS burst and states its limit as well as its
purpose: the OLT estimates the upstream BER from it and only when FEC
is off, since with FEC on the estimate comes from the FEC corrections.

Clause C.10.1.3 makes FEC support mandatory in both directions for both
ends, then splits the control: downstream FEC is statically on and
upstream FEC is under dynamic OLT control per ONU, through the FEC flag
of the selected burst profile. Clause C.10.1.3.2.1 gives the upstream
code as RS(248,216), excludes the PSBu and starts the first codeword at
the FS burst header. All allocations of one ONU share one FEC state and
contiguous allocations are encoded as one block, so a burst has at most
one shortened codeword, at its end. For that codeword the encoder pads
the last data block to 216 bytes with leading zeroes, computes the
parity, removes the padding and transmits the rest and the decoder
reverses the operation (clause C.10.1.3.2.2). Clause C.10.1.3.2.3 asks
the OLT to size allocations into whole FEC blocks and gives the PHY
burst arithmetic: the FS burst, plus a 32-byte parity block for each
whole and one partial 216-byte data block, plus the PSBu. Clause
C.10.1.4.2 scrambles the burst with the downstream polynomial, reset
after the PSBu, with a preload from the superframe counter of the
downstream frame that carried the BWmap.

Grants and the bandwidth map
============================

The BWmap is a series of 8-byte allocation structures (clause
C.8.1.1.2). Each grants one Alloc-ID one interval and a run of
structures belonging to the same ONU and meant for one contiguous
transmission forms a burst allocation series.

::

    allocation structure, 8 bytes = 64 bits

    +-------------+-----+-------------+-------------+---+----+-------+
    |  Alloc-ID   |Flags| StartTime   | GrantSize   |FWI|Prof| HEC   |
    |   14 bits   | 2 b |  16 bits    |  16 bits    |1 b|2 b | 13 b  |
    +-------------+-----+-------------+-------------+---+----+-------+
                    |
                    +-- the DBRu and PLOAMu flags

Clauses C.8.1.1.2.1 to C.8.1.1.2.7 define the fields in that order. The
Alloc-ID names the recipient, a T-CONT or the upstream OMCC of an ONU.
DBRu asks for a buffer report and PLOAMu, meaningful only in the first
structure of a series, asks for a PLOAM message in the burst header.
StartTime says where the burst begins in the upstream PHY frame, as one
of 9 720 equally spaced instants, 0 to 9 719. One step is a 4-byte word
at 2.48832 Gbit/s or a 16-byte block at 9.95328 Gbit/s and names the
same instant either way and only the first structure of a series
carries a real value, the rest carrying 0xFFFF. GrantSize is the
combined length of the FS payload and the DBRu overhead, excluding the
burst header, the trailer and the FEC overhead, in the same units as
StartTime. Zero marks a PLOAM-only grant, which is what serial number
grants and ranging grants are. FWI is the forced wake-up bit,
BurstProfile is the profile index and the HEC is a BCH(63,12,2) code
over the first 63 bits plus one parity bit.

Clause C.6.1.5.7 makes the Alloc-ID 14 bits. Values 0 to 1 020 are default
Alloc-IDs, each implicitly equal to an ONU-ID. Values 1 021 to 1 023 are
broadcast values used in serial number grants. Values 1 024 to 16 383 are
assignable, handed out with the Assign_Alloc-ID PLOAM message. The default
Alloc-ID is not assigned by a message: it exists because the ONU-ID exists.
It carries the upstream OMCC, it may carry user traffic, it is used for
PLOAM-only allocations and it cannot be removed or changed for the life of
the activation cycle. The broadcast values separate line rates during
discovery (Table C.6.5): 1 022 addresses ONUs at 9.95328 Gbit/s and is the
value for XGS-PON discovery, 1 023 addresses ONUs at 2.48832 Gbit/s and
1 021 addresses both but is forbidden when the system interworks with
XG-PON.

Clause C.8.1.1.3 bounds the BWmap. The field allows at most 2 047
structures, but the clause imposes tighter rules.

1. Distinct burst allocation series appear in ascending StartTime order.
2. Burst spacing satisfies the PHY requirements of clause C.10.1.2.
3. The minimum StartTime is zero, so a PSBu can belong to the previous
   PHY frame.
4. The maximum StartTime is 9 719, so a burst can cross the frame
   boundary.
5. At most 512 allocation structures per BWmap.
6. At most 16 allocation structures per burst allocation series.
7. At most 64 allocation structures per ONU per BWmap.
8. At most 16 burst allocation series per XGS-PON ONU per BWmap. The
   clause notes that G.987.3 allows 4 for an XG-PON ONU.
9. The maximum GrantSize is 9 719 sixteen-byte blocks at
   9.95328 Gbit/s and 9 718 four-byte words at 2.48832 Gbit/s.
10. The maximum FS burst size, allocations plus overhead, is 155 520
    bytes at 9.95328 Gbit/s and 38 880 bytes at 2.48832 Gbit/s.
11. StartTime plus the sum of the GrantSizes must not exceed 14 580.

One rule runs through the error cases of the same clause: suppress
transmission rather than risk a collision. An uncorrectable error in an
allocation structure, or an unknown Alloc-ID in its own series, stops the
ONU for the rest of the burst. A violation of rule 4 suppresses the burst
entirely. A violation of rules 5 to 11 makes the ONU cut the transmission
short as if the rule had held. Its own Alloc-ID inside another ONU's series
is ignored.

Clause 5.10 defines the T-CONT as an OMCI managed entity representing a
group of logical connections that appear as one entity for upstream
bandwidth assignment. The number an ONU supports is fixed, the ONU
creates every instance during activation or on an OMCI MIB reset and
the OLT discovers how many over the OMCC. The OLT assigns the Alloc-ID
over PLOAM, then maps a T-CONT to it over the OMCC and that mapping is
what activates the T-CONT for user traffic. The mapping of the OMCC
itself to the default Alloc-ID is fixed, cannot be managed through the
MIB and should survive a MIB reset. The
same clause sets the priority: the correspondence is often one to one,
but it is the Alloc-ID, not the T-CONT, that the TC layer sees and the
recipient of a grant may be an internal non-managed structure instead.
Product documentation talks about T-CONT types 1 to 4. Those numbers are
not a field and G.988 clause II.3.2.4 notes that the numbering is
purely a documentation convenience.

Clause 5.11 separates two words that sound alike: bandwidth assignment
distributes upstream capacity between traffic-bearing entities and is
refined periodically, while bandwidth allocation grants individual
transmission opportunities on the timescale of a single PHY frame, using
the assigned values as input and producing the BWmaps. Clause 3.1.6
defines dynamic bandwidth assignment as the process by which the OLT
distributes upstream capacity based on a dynamic indication of activity
and on the configured traffic contracts and clause C.7.2.2 makes DBA
support mandatory at the OLT with four functions: infer the buffer
occupancy, update the assigned bandwidth within the provisioned
parameters, issue allocations accordingly and manage the operation.
Clause C.7.2.1 states a strong abstraction: whatever the number of
Alloc-IDs per ONU, the number of XGEM ports on each and the real
queueing structure inside the ONU, the OLT models each Alloc-ID as one
logical buffer and treats all Alloc-IDs as independent peers.

Clause C.7.2.3 gives two ways to infer that occupancy. Status reporting
DBA uses explicit buffer occupancy reports, solicited by the OLT and
returned by the ONU. That is the DBRu of the upstream burst. Traffic
monitoring
DBA uses the OLT's own observation of the idle XGEM frame pattern and
needs no ONU cooperation, because an ONU with nothing to send must fill
the allocation with idle XGEM frames (clause C.9.1.4). Clauses 3.2.24
and 3.1.46 define the two the same way and the obligations are
asymmetric: the OLT must support a combination of both and the ONU must
support status reporting. The algorithm itself is deliberately
unspecified: clause C.7.2.3 puts the details of how the OLT applies the
status, the whole traffic monitoring method and the upstream scheduler
outside the scope of the TC layer and Appendix A.I.2.1 adds that the
DBA algorithm is not specified in any standard, that this is not an
interoperability issue and that the omission was intentional. The
Recommendation gives instead a reference model (clause C.7.3) and
criteria to compare an implementation against it, of which clause
C.7.4.2, assured bandwidth restoration time, is one with a number: a few
milliseconds is expected, target 2 ms.

The grant is coarse. It names an Alloc-ID and a number of bytes and no XGEM
port, queue, VLAN or frame. Everything below that is the ONU's decision and
it is the decision a transmit scheduler makes on any shared link. G.988
clause 9.2.10 places priority queues between the GEM ports and the T-CONT,
each with a priority and a weight. Clause 9.2.11 allows a traffic scheduler
in between, whose policy attribute selects strict priority or weighted round
robin. Clause 9.2.12 adds a traffic descriptor with committed and peak
information rates and burst sizes and a color marking the queue uses to
drop. The OLT decides how much upstream capacity an Alloc-ID gets. The ONU
decides which of its own queues fills it. That is where queueing lives.

XGEM, the data plane
====================

Clause C.9.1.1 defines the FS payload as a sequence of XGEM frames, each
a fixed size header and a variable payload and clause C.9.1.2 makes the
header 8 bytes.

::

    XGEM header, 8 bytes = 64 bits

    +---------------+----+-------------------+
    |   PLI, 14 b   |Key |  XGEM Port-ID,    |
    |               |idx |     16 bits       |
    |               |2 b |                   |
    +---------------+----+-------------------+
    |        Options, 18 bits       |LF| HEC |
    |                               |1b| 13b |
    +-------------------------------+--+-----+

Clause C.9.1.2 defines every field. PLI is the length L in bytes of the
SDU or fragment that follows. Its range is 0 to 16 383, which the clause
notes covers an expanded Ethernet frame up to 2 000 bytes and a jumbo
frame up to 9 000 bytes. The key index is 00 unencrypted, 01 first key,
10 second key, 11 reserved, with unicast or broadcast following from the
Port-ID and a reserved or invalid value making the receiver discard the
payload. The Port-ID names the logical connection. The 18 option bits
are for further study: the sender writes zero and the receiver ignores
them. LF is set for a complete SDU or the last fragment of one and the
HEC is a BCH(63,12,2) code over the first 63 bits plus one parity bit.
Clause C.9.1.3 relates the payload length to the PLI with an equation
and adds that the payload may carry one to seven padding bytes in its
least significant byte positions, filled with 0x55 by the transmitter
and discarded by the receiver.

The Port-ID is the flow label of the system. Clause C.6.1.5.8 makes it 16
bits, assigned by the OLT to one logical connection: values 0 to 1 020 are
default Port-IDs, each numerically equal to an ONU-ID and carrying that
ONU's OMCC. Values 1 021 to 65 534 are assignable over the OMCC, not over
PLOAM. Value 65 535 is the idle Port-ID. An ONU re-entering state O1
discards the default Port-ID but keeps the non-default ones and Table C.6.6
withholds values 1 021 and 1 022 from XG-PON ONUs. Downstream the Port-ID is
a filter: clause C.6.1.5.1 states that the OLT multiplexes on it, that each
ONU processes only the frames whose Port-ID belongs to it and that a
multicast port can reach several ONUs. Upstream it is a classification
result: the ONU decides which port an Ethernet frame belongs to and puts
that port inside the interval it was granted.

Clause C.9.1.4 defines the idle frame as any XGEM frame with Port-ID
0xFFFF. A transmitter with nothing to send fills the remaining payload
with idle frames and the clause counts a non-work-conserving scheduler
as having nothing to send. The PLI of an idle frame is the real payload
size, any multiple of 4 including 0, up to the maximum supported SDU
size. Idle frames go unencrypted with LF
set to 1 and the receiver ignores the key index, the LF bit and the
payload. If only 4 bytes remain, too few for a header, the transmitter
emits a short idle frame, defined as four all-zero bytes.

Clause C.9.2 defines delineation and it is not a hunt for a sync
pattern. The receiver knows a XGEM header starts the FS payload, reads
the PLI, computes where the next header starts and confirms the guess
by verifying the HEC of that next header. If that fails, it discards the
current frame and the rest of the FS payload. One bad header therefore
costs the rest of the frame or burst, which is why Table C.14.1 pairs a
count of XGEM header HEC errors with an optional count of FS frame words
lost to them.

Clause C.9.3 defines fragmentation. Downstream, the OLT fragments at its
discretion: if at least 16 bytes of payload remain and the SDU plus its
8-byte header does not fit, the SDU is split so the first fragment exactly
fills the current frame and once split, the second fragment goes before any
other SDU, so there is no downstream pre-emption. Upstream, an ONU in
substate O5.1 fragments without extra restrictions, filling the current
allocation and continuing in the next allocation of the same Alloc-ID and
within one Alloc-ID there is no pre-emption either. Three rules apply both
ways. A second fragment shorter than 8 bytes is padded to 8, so that no XGEM
frame is smaller than 16 bytes. If the SDU and its header already fit,
further fragmentation is forbidden. If fewer than 16 bytes remain, the space
is filled with an idle frame.

Clause C.9.4.1 maps Ethernet onto XGEM: the frame goes directly into the
XGEM payload, the preamble and start frame delimiter are discarded
first, one Ethernet frame maps to one XGEM frame or to several under the
fragmentation rules and one XGEM frame never carries more than one
Ethernet frame. Clause C.9.4.2 maps MPLS packets the same way and
clause C.9.4 leaves other services for further study. G.9807.1 states no
numeric maximum SDU size: clause C.9.1.4 refers to "the maximum
supported SDU size" without a value. Two clauses bound the problem
instead: clause C.9.1.2 caps any single XGEM payload at the 14-bit PLI,
so 16 383 bytes. Clause A.7.5 requires support for Ethernet jumbo
frames beyond 2 000 bytes and up to 9 000 bytes without degrading the
delay-sensitive services sharing the PON. G.988 clause 9.2.7 gives the
per-flow control, the maximum GEM payload size attribute of the GAL
Ethernet profile.

Encryption of user data
=======================

Clause C.15.4.1 specifies AES-128 in counter mode for XGEM payload
encryption: the cipher runs forward over a sequence of counter blocks
and the output is exclusive-ORed with the payload, the sequence starting
for every XGEM frame at an initial counter block. Only the payload is
encrypted. The header is not, which is what lets an ONU filter on the
Port-ID and lets any receiver delineate.

Clause C.15.4.3 builds the initial counter block from two counters. The
first is the superframe counter: downstream from the PSBd of the frame
carrying the XGEM frame, upstream from the PSBd of the frame that
carried the BWmap specifying the burst. The clause omits its most
significant bit and uses a 50-bit field, although clause C.10.1.1.1.2
makes the counter itself 51 bits. The second is the intra-frame counter,
14 bits: downstream the FS frame is cut into 16-byte blocks numbered 0
to 8 464, the last half size and upstream the FS burst is cut into
blocks numbered from S to S + X, where S is the StartTime at
9.95328 Gbit/s. The intra-frame counter of a XGEM frame is the number of
the block holding the first four bytes of its header. The two directions
differ by one operation: the clause concatenates the two counters, then
concatenates that with itself downstream and with its own
bit-complement upstream, so the same counter values do not produce the
same key stream in both directions. The clause records one exception in
a note: two superframe counter values can still duplicate counter blocks
across the directions, a window of about 250 microseconds once in 4 000
years, which initializing the counter to a small value avoids.

Clause C.15.4.2 pairs the keys: one PON-wide broadcast key pair for the
broadcast Port-IDs and one unicast key pair per ONU for that ONU's
Port-IDs, with two keys of each type valid at once, which is what the
key index selects. Clause C.15.5.1 states who generates what: the ONU
generates the unicast keys and reports them over PLOAM, the OLT
generates the broadcast keys and writes them to each ONU over OMCI and
the unicast key value is never exposed to OMCI. For each non-default
port the OLT writes an encryption key ring attribute on the GEM port
network CTP managed entity, whose values G.988 clause 9.2.3 gives as 0
no encryption, 1 unicast both ways, 2 broadcast, 3 unicast downstream
only. The default port has no configurable key ring and is defined for
bidirectional encryption with the unicast key. Clause C.15.5.1 separates
provisioning from use: a port provisioned for encryption is not always
encrypted and the sender decides per frame, within what the port
allows and says so in the header. The same clause gives the fallback:
when no valid data encryption key is available, for instance just after
a reactivation, the sender transmits unencrypted with a key index of 0.

PLOAM and activation
====================

Clause C.11.1 describes the PLOAM channel as a fixed set of 48-byte
messages carried in band, in the PLOAMd partition downstream and in the
FS burst header upstream and clause C.11.1.1 lists its jobs: profile
announcement, ONU activation, ONU registration, encryption key update
exchange and power management.

=====================  ===============  =====================================
Octets (Table C.11.1)  Field            Notes
=====================  ===============  =====================================
1-2                    ONU-ID           10 bits, LSB aligned, 6 reserved bits
3                      Message type ID  the code points below
4                      SeqNo            sequence number
5-40                   Message_Content  per message type, padded with 0x00
41-48                  MIC              message integrity check
=====================  ===============  =====================================

Clause C.11.2.3 gives the sequence number rules. Downstream, the OLT
keeps one counter per ONU for unicast and one for broadcast. The
broadcast counter starts at 1 when the OLT reboots, a unicast counter
starts at 1 when the OLT assigns the ONU-ID, each transmission
increments the counter and each counter rolls from 255 to 1 because 0
is not used downstream. Upstream, a response repeats the number it
answers and the same number may appear on more than one message, for
instance when a key is sent in fragments. An autonomous upstream message
uses 0 and so does a response to a PLOAM grant with nothing to send.

Clause C.11.1.2 gives the timing rules. Within one 125 microsecond frame
the OLT may send at most one broadcast message and at most one unicast
message per ONU and the ONU should be able to store eight messages
before processing them. Processing is single threaded and the normative
processing time is 750 microseconds: for a message received into an
empty queue in downstream frame N, the ONU should be able to produce its
response no later than upstream frame N+6. If the queue is still not
empty when a response goes out, the next message should be processed
within the following six upstream frames. Upstream messages leave only
when the OLT sets a PLOAMu flag, so the OLT controls the upstream rate,
using among other things the PLOAM queue status bit of the Ind field.

Clause C.11.1.3 gives the robustness rules. When unicast message
processing leaves the ONU in state O5, the ONU acknowledges, either with
the specific type the protocol calls for or with a generic
Acknowledgment, which is also the answer to a processing error and to a
PLOAM grant with nothing queued. Its completion code separates the idle
case from the busy case. Broadcast messages needing no response are not
acknowledged and neither are messages that fail the integrity check.
Key_Control is the exception, requiring a response even when broadcast.
Repeated failure to acknowledge becomes the LOPCi defect.

====  =================================  ====================================
ID    Downstream message (Table C.11.2)  Purpose
====  =================================  ====================================
0x01  Burst_Profile                      publish upstream burst parameters
0x03  Assign_ONU-ID                      bind an ONU-ID to a serial number
0x04  Ranging_Time                       set or adjust the equalization delay
0x05  Deactivate_ONU-ID                  stop upstream traffic, reset the ONU
0x06  Disable_Serial_Number              disable or re-enable an ONU, or all
0x09  Request_Registration               ask for the Registration_ID
0x0A  Assign_Alloc-ID                    assign or cancel an Alloc-ID
0x0D  Key_Control                        generate a new data key, or confirm
0x12  Sleep_Allow                        permit or withhold power saving
0x1D  Reboot_ONU                         reboot one ONU or all of them
====  =================================  ====================================

====  ===============================  ====================================
ID    Upstream message (Table C.11.3)  Purpose
====  ===============================  ====================================
0x01  Serial_Number_ONU                report the serial number of an ONU
0x02  Registration                     report the Registration_ID
0x05  Key_Report                       send a key fragment, or a key name
0x09  Acknowledgment                  acknowledge, report an error or idle
0x10  Sleep_Request                    ask to start or end power saving
====  ===============================  ====================================

Clauses C.11.3.1 and C.11.3.2 reserve identifiers 0xF0 to 0xFF in each
direction for ITU-T G.9807.2.

====================  =================  ======================================
State (Table C.12.1)  Name               Meaning in one line
====================  =================  ======================================
O1                    Initial            transmitter off, TC configuration gone
O1.1                  Off-Sync           searching for the downstream signal
O1.2                  Profile Learning   synchronized, collecting profiles
O2-3                  Serial Number      answering serial number grants
O4                    Ranging            answering ranging grants, TO1 running
O5                    Operation          processing frames, transmitting
O5.1                  Associated         entry substate, fragmentation free
O6                    Intermittent LODS  downstream lost, TO2 running
O7                    Emergency Stop     laser off by order, survives reboot
====================  =================  ======================================

Table C.12.2 defines the timers: TO1 limits the time in O4, with a
recommended initial value of 10 seconds and TO2 limits the time in O6,
for which G.9807.1 gives no numeric value.

Clause C.12.1.2 splits activation into three phases: downstream
synchronization, serial number acquisition and ranging. The ONU powers
up into O1.1 with its transmitter off, except that Table C.12.4 returns
it to O7 if that was the last operational state. In O1.1 it runs the
synchronization state machine and stays silent. On synchronization it
moves to O1.2 and parses the PLOAMd partition for Burst_Profile
messages, leaving for O2-3 only when it has collected enough profile
information. In O2-3 it waits for a serial number grant and answers
with a Serial_Number_ONU message carrying its Vendor_ID, its
vendor-specific serial number and the random delay it used. Clause
C.12.1.5 defines that grant as an allocation to a broadcast Alloc-ID,
with a commonly known broadcast burst profile, with the PLOAMu flag set,
the DBRu flag clear and GrantSize zero, accompanied by a quiet window.

The quiet window is how the OLT makes room for an answer from an ONU
whose distance it does not know. Clause C.13.1.2 explains it: the OLT
suppresses transmission by the ONUs in service so that a serial number
response cannot collide with them and since several ONUs may answer one
grant, each adds a locally generated random delay in the range 0 to 48
microseconds, expressed in bit periods at 2.48832 Gbit/s whatever the
ONU's own rate, fresh for every response. The clause sizes the window
from the unknown components: for 20 km differential distance, 200
microseconds of round trip propagation variation, 2 microseconds of
response time variation and 48 microseconds of random delay, suggesting
250 microseconds and for 40 km the propagation term doubles and the
suggestion becomes 450 microseconds. Clause C.13.1.7 states the cost:
each quiet window touches two and possibly three consecutive BWmaps, so
the OLT must limit the damage to bandwidth and to jitter-sensitive
flows, for instance by rearranging the maps and adding allocations just
before and just after.

When the OLT recognizes the serial number it sends Assign_ONU-ID (Table
C.11.6), addressed to the broadcast ONU-ID and naming the serial number,
so the ONU owning it takes the identifier, in the range 0 to 1 020.
Clauses C.6.1.5.7 and C.6.1.5.8 then give that ONU its default Alloc-ID
and its OMCC Port-ID for free, both numerically equal to the ONU-ID. The
ONU starts TO1 and enters O4 and nothing is acknowledged. In O4 it
treats any directed allocation with PLOAMu set as a ranging grant and
answers with a Registration message. Clause C.13.1.3 sizes the ranging
quiet window from the same components minus the random delay, since the
requisite delay during ranging is zero: 202 microseconds suggested for
20 km differential distance and 402 for 40 km, which the OLT may shrink
if it already has a distance estimate.

Clause C.13.1.4 defines the measurement: the OLT times the interval
between the downstream frame carrying the ranging grant and the upstream
burst carrying the Registration message and derives the equalization
delay from it, expressed in integer bit periods at 2.48832 Gbit/s
whatever the ONU's rate, with an ONU adjustment granularity no coarser
than 8 bit periods. Clause C.13.1.1 gives the other half of the sum:
every ONU must have a response time of 35 plus or minus 1 microseconds
and must know its own value and it keeps an upstream frame clock offset
from its downstream frame clock by the response time plus the requisite
delay, which in O5 is the assigned equalization delay. Once it has the
delay it is synchronized to the upstream frame and transitions to O5.
Clause C.12.1.5 allows two shortcuts: the OLT may send Assign_ONU-ID to
a known serial number, so the ONU passes through O2-3 without answering
a serial number grant and it may send Ranging_Time with a previously
measured value, so the ONU passes through O4 without a ranging response.

Explicit Alloc-IDs come after activation: Assign_Alloc-ID carries a
14-bit value and a type octet, 0x01 for XGEM-encapsulated payload and
0xFF to deallocate (Table C.11.11) and clause C.6.1.5.7 makes these
revertible, unlike the default. Registration is the last piece. Table
C.11.25 makes the message carry a 36-octet Registration_ID, recommended
to be ASCII and padded with 0x00 and clause C.15.2.1 requires it to
live in non-volatile storage at the ONU, to survive reactivation and
power cycling and to change only when a person changes it.

Ranging does not stop at activation. Clause C.13.1.6 describes
in-service adjustment, because arrival phase drifts with aging and
temperature: crossing the lower of two thresholds makes the OLT compute
a new delay, send a relative Ranging_Time and record a DOWi event and
crossing the upper one, which should not happen if the ONU obeys the
adjustments, makes it declare TIWi and act more strongly. Table C.13.1
gives the suggested thresholds at 9.95328 Gbit/s as plus or minus 32
bits for DOWi and plus or minus 64 bits for TIWi, about 3.2 ns and
6.4 ns.

Deactivate_ONU-ID makes the addressed ONU switch its laser off, discard
the ONU-ID, the Alloc-IDs, the default Port-ID, the burst profiles and
the equalization delay and return to O1 (Table C.11.2).
Disable_Serial_Number is harsher. Table C.11.9 gives its codes: 0xFF
denies upstream access to the ONU with the named serial number, 0x00
allows it, 0x0F denies access to all ONUs and 0xF0 allows all. A
disabled ONU switches its laser off and goes to O7, where it keeps the
synchronization state machine running and keeps parsing PLOAM, but may
neither forward downstream data nor send anything upstream. Table C.12.1
states what makes O7 an emergency stop rather than a reset, that the
state persists over reboot and power cycle and that only
Disable_Serial_Number with the enable option brings it back to O1.
Losing the downstream signal from O5 is gentler: Table C.12.1 sends the
ONU to O6 and starts TO2 and if the downstream returns before TO2
expires the ONU goes back to O5.1 and keeps its configuration, while if
TO2 expires it discards the configuration and returns to O1.1.

Power management uses the same channel. Clause C.16 defines three modes,
Doze, Cyclic sleep and Watchful sleep and splits the obligation: an
XGS-PON OLT must support all three to carry XG-PON ONUs, while an
XGS-PON ONU need support only Watchful sleep. Clause C.16.1.1 describes
the signaling: OMCI configures the capability, PLOAM messages carry the
transitions and the FWI bit in a BWmap allocation is the fast path to
wake an ONU. Table C.16.2 names the ONU states, ActiveHeld, ActiveFree,
Aware and LowPower. In LowPower the transmitter is off and while the
ONU checks the downstream for a wake-up indication it neither answers
grants nor forwards traffic.

Type B protection puts two OLT ports on one ODN. Clause C.18.1 describes
it: a 2:N splitter replaces the 1:N splitter and two feeder fibers run
to two OLT ports, in one chassis or in two. Exactly one port may
transmit, so each runs a state machine with a standby role (states
Initialization, Protecting and LOS-P) and an active role (states
Pre-Working, Working and LOS-W) (Table C.18.1). A Thold timer
locks the active role down so that the two ports do not flap. Clause
C.11.3.3.3 notes the interaction with ranging: a broadcast Ranging_Time
can apply a signed delay offset to every ONU at once, which is what a
protection switch needs.

Authentication and the key hierarchy
====================================

Clause C.15.2 defines three authentication mechanisms. The first is
registration-based, runs during activation and may repeat until the ONU
next enters O1. Support is mandatory in all XGS-PON systems and clause
C.15.2 is blunt about its limit, that it authenticates the ONU to the
OLT and not the other way round. The other two are secure mutual
authentication, one over OMCI and one over IEEE 802.1X, with an unusual
support rule: mandatory at component level and optional at equipment
level, so a TC layer must be able to do them while a product need not
offer them. Clause C.15.2.1 lists what registration-based authentication
needs: a Registration_ID assigned at management level, provisioned into
the OLT, communicated to the installer or the subscriber, a way to enter
it at the ONU and a person who does so. The entry method is out of
scope. Clause C.15.2.1.1 states that whether the OLT authenticates on it
is up to the operator and that failure must not stop the OLT issuing an
equalization delay, so the ONU still reaches O5 and the consequence
falls on service provisioning.

The registration id does a second job whatever the operator decides: it
seeds the key hierarchy. Clause C.15.3.1 makes AES-CMAC with AES-128 the
one primitive, taking a key, a message and an output length.

::

     Registration_ID (36 bytes)
           |
           | MSK = AES-CMAC((0x55)16, Registration_ID, 128)     C.15-2
           v
         MSK (128 bits)
           |
           | SK = AES-CMAC(MSK, SN | PON-TAG | "SessionK", 128) C.15-3
           v
         SK (128 bits)
           |
           +--> OMCI_IK  = AES-CMAC(SK, "OMCIIntegrityKey", 128)  C.15-4
           +--> PLOAM_IK = AES-CMAC(SK, "PLOAMIntegrtyKey", 128)  C.15-5
           +--> KEK      = AES-CMAC(SK, "KeyEncryptionKey", 128)  C.15-6

In clause C.15.3.2, (0x55)16 is the hex pattern 0x55 repeated sixteen
times. Clause C.15.3.3 names the 24-byte message of the session key
formula: the 8-byte serial number from octets 5 to 12 of
Serial_Number_ONU, the 8-byte PON-TAG from octets 26 to 33 of
Burst_Profile and the 8 bytes 0x53657373696f6e4b, the ASCII string
"SessionK", so that the session key binds the master key to this OLT and
this ONU. The same clause derives the other three from the session key
and a 16-byte ASCII constant each, 0x4f4d4349496e746567726974794b6579
for "OMCIIntegrityKey", 0x504c4f414d496e7465677274794b6579 for
"PLOAMIntegrtyKey" and 0x4b6579456e6372797074696f6e4b6579 for
"KeyEncryptionKey", states that the missing letter in
"PLOAMIntegrtyKey" is deliberate so that the string is exactly 16 bytes
and requires the ONU to re-derive all four keys when the PON-TAG
changes. It also defines a default PLOAM integrity key equal to
(0x55)16, which clause C.15.8.1 applies before any master key exists and
thereafter to downstream broadcast messages and to the unicast messages
that must work when the two ends disagree about the derived keys, naming
Serial_Number_ONU, Deactivate_ONU-ID, Request_Registration and
Registration as examples. OMCI needs no default key, because no OMCI
exchange happens before the master key exists and there is no broadcast
OMCC.

The data encryption key is separate and travels the other way. Clause
C.15.5.3.1 gives the exchange: the OLT sends Key_Control with the control
flag clear, meaning generate and a key index. The ONU generates a key with a
random number generator suitable for cryptographic purposes, stores it and
returns it in a Key_Report message, wrapped as KEK_Encrypted_key =
AES_ECB_128(KEK, encryption_key) (Table C.11.26). The OLT decrypts and
stores it, then sends Key_Control with the flag set, meaning confirm. The
ONU activates the key for transmit and answers with a Key_Report carrying
not the key but its name, defined in the same table as Key_Name =
AES_CMAC(KEK, encryption_key | 0x33313431353932363533353839373933, 128). The
OLT compares that with its own computation and, on a mismatch, stops using
the key and takes remedial action. Table C.11.26 carries the key in
fragments, with a three-bit fragment number and a 32-byte fragment field and
its note says AES-128 and its extension to AES-192 or AES-256 need one
fragment.

The two integrity checks share one construction and differ in key, in
span and in length. Clause
C.15.6.2 gives PLOAM-MIC = AES-CMAC(PLOAM_IK, Cdir | PLOAM_CONTENT, 64),
where PLOAM_CONTENT is octets 1 to 40 of the message. Clause
C.15.7.2 gives OMCI-MIC = AES-CMAC(OMCI_IK, Cdir | OMCI_CONTENT, 32),
where OMCI_CONTENT is the message except its last four bytes. Both
define the direction code the same way: Cdir is 0x01 downstream and 0x02
upstream, which is what stops an attacker replaying a downstream message
upstream.

Clause C.11.2.5 makes the MIC cover padding octets as well as
significant ones and declares a message whose MIC does not match
invalid and to be discarded. That has a consequence for where the keys can live.
Clause C.11.1.2 gives the ONU 750 microseconds to process a PLOAM
message and produce a response and every message in both directions
needs a CMAC over 41 bytes. Clause C.15.4.3 goes further: the counter
block for each XGEM frame is built from the superframe counter of the
current frame and the position of that frame inside the frame or burst,
both of which exist only inside the framing engine and change per frame
at 9.95328 Gbit/s. The design therefore puts the keys where the framing
engine can reach them, rather than where a slower control path would
fetch them. That is a consequence of how the Recommendation is built,
not a claim about any particular implementation.

OMCI
====

The channel and the message formats
-----------------------------------

Clause C.6.1.4.3 defines the OMCC as a XGEM-based transport for OMCI
messages, with an adapter at each end filtering, de-encapsulating and
encapsulating and clause C.6.1.5.8 makes the OMCC Port-ID numerically
equal to the ONU-ID, so it needs no assignment. G.988 clause B.1 states
the same from the management side, adds that the default Alloc-ID needs
no T-CONT and adds that if subscriber traffic shares that Alloc-ID,
OMCI traffic takes strictly higher priority. Clause 11.2 states that
each OMCI message goes in one GEM frame, or several under the normal
fragmentation rules.

Clause 11.1 defines two formats, baseline with a fixed 48-byte packet
and extended with a variable one. Clause 11.2 caps the extended packet
at 1 980 bytes. Baseline is the
default at initialization and the extended format is negotiated: the OLT
reads the OMCC version attribute of the ONU2-G to learn whether extended
messages are supported and an ONU may not send one, including an
autonomous notification, until it has received one from the OLT in the
current session.

::

    baseline OMCI message, 48 bytes

    +-------+----+----+-----------+------------------+---------------+
    | TCI   | MT |Dev | ME id     | message contents | trailer       |
    | 1-2   | 3  | 4  | 5-8       | 9-40, 32 bytes   | 41-48, 8 bytes|
    +-------+----+----+-----------+------------------+---------------+

    extended OMCI message, N bytes, N <= 1 980

    +-------+----+----+-----------+------+-------------+-------------+
    | TCI   | MT |Dev | ME id     | len  | contents    | MIC         |
    | 1-2   | 3  | 4  | 5-8       | 9-10 | 11..(N-4)   | (N-3)..N    |
    +-------+----+----+-----------+------+-------------+-------------+

    message type byte:  bit 8 = 0, bit 7 = AR, bit 6 = AK, bits 5..1 = MT

Clause 11.2.1 defines the transaction correlation identifier: the OLT
picks it for a request, a response repeats it, an autonomous message
from the ONU uses 0 and in the baseline format only its most
significant bit is a priority. Clause 11.2.2 defines the message type
byte. Bit 8 is reserved and always 0. Bit 7, the acknowledge request, is
set when the sender expects a response and is always 0 from the ONU. Bit
6, the acknowledgment, is set when the message is a response and is
always 0 from the OLT. Bits 5 to 1 are the action. Clause 11.2.3 defines
the device identifier as 0x0A baseline and 0x0B extended, in the same
byte position in both, so a receiver can tell them apart before parsing
anything else. Clause 11.2.4 defines the managed entity identifier as
two bytes of class value and two bytes of instance. Clause 11.2.7
defines the 8-byte baseline trailer: two bytes set to zero and ignored,
two bytes of length fixed at 0x0028, that is 40, then the MIC. Clause
11.2.8 makes the extended MIC four bytes and clause 11.2.5 limits the
extended contents to 1 966 bytes. Neither states which bytes the MIC
covers, delegating that to the TC layer specification, which for XGS-PON
is clause C.15.7.2.

==  =======================  ================  ======================
MT  Action (Table 11.2.2-1)  Acknowledged      MIB data sync
==  =======================  ================  ======================
4   Create                   yes               yes
6   Delete                   yes               yes
8   Set                      yes               on a successful change
9   Get                      yes               no
11  Get all alarms           yes               no
12  Get all alarms next      yes               no
13  MIB upload               yes               no
14  MIB upload next          yes               no
15  MIB reset                yes               no
16  Alarm                    no                no
17  Attribute value change   no                no
18  Test                     yes               no
19  Start software download  yes               yes
20  Download section         last of a window  no
21  End software download    yes               yes
22  Activate software        yes               yes
23  Commit software          yes               yes
24  Synchronize time         yes               no
25  Reboot                   yes               no
26  Get next                 yes               no
27  Test result              no                no
28  Get current data         yes               no
29  Set table                yes               yes
==  =======================  ================  ======================

Set table exists only in the extended message set.

The MIB, notifications and the object model
-------------------------------------------

Clause 9.1.3 models the MIB itself as a managed entity, ONU data, with
exactly one instance, number 0 and one attribute besides its
identifier: MIB data sync, an 8-bit sequence number. MIB reset clears
the MIB, re-initializes it to its default and sets MIB data sync to 0.
The default MIB is the mandatory managed entities plus the auto-created
ones implied by the architecture of the ONU and for GPON that minimum
is one ONU-G pair, one ONU data and two software image instances. MIB
upload latches a snapshot and MIB upload next walks it: the upload
response carries the number of subsequent upload next commands (clause
A.3.14) and each upload next response carries an entity class, an
entity instance, an attribute mask and the attribute values (clause
A.3.16). Clause I.1.3.2 allows at most one minute between two upload
next requests, after which the ONU may drop the snapshot and lists what
an upload omits: table attributes, the measurement attributes of PM
managed entities and some general purpose entities.

Clause I.1.2 defines MIB data sync and it is narrower than the name
suggests. It increments once per executed OLT command that changes the MIB,
not once per changed attribute, so setting it to N leaves it at N+1. It does
not increment for autonomous change at the ONU. It rolls from 255 to 1.
Value 0 is reserved for the factory default MIB and for an ONU that cannot
restore its MIB. The same clause draws the useful consequence: if the OLT
changes its own copy while the ONU is offline, its counter moves and the
next audit is guaranteed to fail, which also forces reconciliation when an
ONU is replaced. Clause I.1.3.1 gives three occasions for an audit: on loss
and re-establishment of the OMCC, periodically and on demand. The audit is
one get of MIB data sync. Clause I.1.3.2 warns that a match is not proof
(the attribute does not reflect the whole MIB) and recommends periodic
resynchronization regardless.

Clause 9 states that alarms, threshold crossing alerts and autonomous
self-test failures all arrive as alarm messages and that the alarm
message carries 224 bits, of which up to 208 are alarms defined per
managed entity class and the last 16 are vendor-specific. Because the
message carries the class and the instance, different classes reuse bit
numbers. Clause A.3.19 lays out the baseline alarm message: class and
instance in bytes 5 to 8, a 28-byte alarm bit map in bytes 9 to 36,
padding and an alarm sequence number in byte 40. Clause A.3.20 lays out
the attribute value change message, class and instance then a two-byte
attribute mask and the new values. The mask has 16 bits, matching
attributes in order from 1, because a managed entity cannot have more
than 16 attributes besides its identifier and for a table attribute the
notification carries only the mask, so the OLT must follow with a get
and a series of get next. Tests are a request and a deferred result:
clause 11.2.2 lists Test as an acknowledged action and Test result as an
unacknowledged notification and clause B.2 explains the split, since
responses should not exceed one second.

Clause 11.2.4 defines the object model: two bytes of class value select
the type and two bytes of instance select the object and the maximum
number of classes is 65 535 because 0 is not used. The same clause warns
that the class table keeps every value ever standardized, including
deprecated ones. Clause 9.1.5 defines the slot convention that many
identifiers use: slot 0 is a universal pseudo-slot in an integrated ONU
and equipment slots run from 1 to 254.

======================  ==================================================
Class (Table 11.2.4-1)  Managed entity
======================  ==================================================
2                       ONU data
7                       Software image
11                      Physical path termination point Ethernet UNI
45                      MAC bridge service profile
47                      MAC bridge port configuration data
84                      VLAN tagging filter data
130                     IEEE 802.1p mapper service profile
171                     Extended VLAN tagging operation configuration data
256                     ONU-G
257                     ONU2-G
262                     T-CONT
263                     ANI-G
264                     UNI-G
266                     GEM interworking termination point
268                     GEM port network CTP
272                     GAL Ethernet profile
277                     Priority queue
278                     Traffic scheduler
280                     Traffic descriptor
281                     Multicast GEM interworking termination point
329                     Virtual Ethernet interface point
332                     Enhanced security control
======================  ==================================================

The provisioning chain
----------------------

The chain runs from the grant to the socket. Each link is a managed
entity with a pointer to the next.

::

      ANI side                                            UNI side

      T-CONT (262)   Alloc-ID attribute, matched to the PLOAM grant
        +<--- traffic scheduler (278)  <--- priority queue (277)
        |        strict priority or WRR              ^  upstream
      GEM port network CTP (268) --------------------+
        |  Port-ID, T-CONT pointer, queue pointers, key ring
        v
      GEM interworking termination point (266)
        |  +--- GAL Ethernet profile (272): maximum GEM payload size
        v
      IEEE 802.1p mapper service profile (130), eight P-bit branches
        v
      MAC bridge port configuration data (47)
        |  +--- VLAN tagging filter data (84)
        |  +--- extended VLAN tagging operation config data (171)
        v
      MAC bridge service profile (45)
        v
      MAC bridge port configuration data (47)
        v
      PPTP Ethernet UNI (11)  or  virtual Ethernet interface point (329)

Clause 9.2.3 defines the GEM port network CTP as the termination of a
GEM port in the ONU, created and deleted by the OLT, with the Port-ID, a
T-CONT pointer, a direction of UNI-to-ANI, ANI-to-UNI or bidirectional,
an upstream traffic management pointer naming either a priority queue or
the T-CONT, a downstream priority queue pointer and the encryption key
ring described above. Clause 9.1.1 defines the traffic management option
on
the ONU-G as 0 priority controlled, 1 rate controlled or 2 both. The ONU
sets it to describe its own hardware and the OLT must adapt its model
to that choice, not the reverse. Clause 9.2.4 defines the GEM
interworking termination point as the place where a bearer service,
usually Ethernet, becomes GEM frames and back. Its interworking option
selects the service, 1 a MAC bridged LAN, 5 an IEEE 802.1p mapper, 6
downstream broadcast and its GAL profile pointer names a GAL Ethernet
profile whose one interesting attribute is the maximum GEM payload size
(clause 9.2.7). Clause 9.3.10 defines the IEEE 802.1p mapper service
profile as eight pointers, one per priority bit value, each naming a GEM
interworking termination point, with the null pointer 0xFFFF meaning
discard.

Clause 9.3.1 defines the MAC bridge service profile as a whole bridge,
with learning, spanning tree and aging and clause 9.3.4 defines a
bridge port by a type and a pointer, so one port can be a PPTP Ethernet
UNI, a GEM interworking termination point, an IEEE 802.1p mapper, a
multicast GEM interworking termination point or a virtual Ethernet
interface point. Two managed entities carry the VLAN rules. Clause
9.3.11 defines VLAN tagging filter data, attached to a bridge port and
states the ordering plainly: tag filtering happens closer to the bridge
than the tagging operation. Clause 9.3.13 defines extended VLAN tagging
operation configuration data, whose rule table always describes the
upstream direction whatever it is attached to. Each rule has a filtering
part and a treatment part and rules fall into three categories by tag
count, zero-tag, single-tag and double-tag. Its downstream mode
attribute decides downstream behavior, from 0, the inverse of the
upstream operation, through filtering on the VLAN identifier, on the
priority, or on both, to 8, discard everything downstream. Clause 9.5.1
defines the PPTP Ethernet UNI as the point where the Ethernet physical
path terminates, one per port.

The VEIP
--------

Clause 9.5.5 defines the virtual Ethernet interface point as the data
plane hand-off point to a separate, non-OMCI management domain, created
and deleted by the ONU rather than the OLT because the downstream
priority queues behind it are a physical constraint and expected one
per non-OMCI domain. Clause II.2 explains why it exists: when the ONU
and the subscriber's equipment are separate boxes the demarcation is the
Ethernet cable between them and when they are one box there is no
cable. It defines a dual-managed ONU as two management domains that may
control the same physical device and makes the VEIP the data plane
demarcation, with OMCI managing everything from the VEIP to the ANI and
the protocol on the other side unspecified. That maps onto the two
deployment styles named above: in an SFU the hand-off is a physical
port, so the chain ends at a PPTP Ethernet UNI and the subscriber's
equipment lies outside the ONU's management, while in an HGU the routing
function is in the same box, so the chain ends at a VEIP and another
system provisions the router behind it. Clause II.2 notes the cost, that
several traffic classes cross the virtual interface so both sides may
have to separate and remerge traffic by class and the benefit, that the
presence of a VEIP tells the OLT what the ONU is during MIB discovery.

Defects, alarms and performance
===============================

Clause C.14.2 states its own scope: it captures the required TC layer
actions and leaves the handling of repeated defects to the implementer.
The defects are mostly what the OLT sees, because the OLT is the only
element that can compare one ONU with the others.

======  =====  ====================================================
Defect  Where  Detection condition
======  =====  ====================================================
LOBi    OLT    Clobi consecutive bursts fail to delineate
LOS     OLT    no upstream at all for four frames
TIW     OLT    drift outside the outer threshold, three corrections
SUFi    OLT    serial number seen, bring-up never completes
DFi     OLT    ONU answers after an attempt to disable it
LOPCi   OLT    persistent PLOAM breakage, three times
LOOCi   OLT    persistent upstream MIC failure, seen by OMCI
LODS    ONU    synchronization machine in Hunt or Pre-Sync
======  =====  ====================================================

Tables C.14.2 and C.14.3 hold those conditions. The LOBi threshold Clobi is
configurable and defaults to four, the condition covers a failure to
delineate for any reason and it is waived when the power management state
machine exempts the ONU. LOS means complete PON failure. TIW follows three
unsuccessful Ranging_Time corrections. LOPCi covers repeated upstream MIC
failure or missing acknowledgments. LODS is the only defect an ONU raises
about itself and Table C.14.3 states its actions: give a visual indication,
signal on the user-side interface and execute the matching activation state
transition. Table C.14.2 pairs LOBi with a caution worth repeating: the
report should be qualified by any dying gasp received, so that a subscriber
pulling a plug does not look like a fiber fault. Defects are levels, not
edges: most entries pair a detection condition with a cancellation condition
that is its plain inverse, so a received burst clears LOBi, one upstream
transmission clears LOS, drift back inside the lower threshold clears TIW
and synchronization clears LODS. The LOPCi and LOOCi rows state no
cancellation condition. Nothing in either table counts events.

The ONU reports upward over OMCI, in a different vocabulary. G.988
clause 9.2.1 gives the ANI-G seven alarms: low and high received optical
power, signal fail, signal degrade, low and high transmit optical power
and laser bias current. Signal fail and signal degrade are configurable
BER thresholds: signal fail is an exponent y giving a BER of 10 to the
power minus y, valid from 3 to 8, default 5 and signal degrade is an
exponent x, valid from 4 to 10, default 9, which must be lower than the
signal fail threshold. The same clause records an industry expectation
that is not a requirement: the BER normally has to improve by an order
of magnitude before the alarm clears. G.988 clause 9.1.1 gives the ONU-G
sixteen alarms, about the box rather than the line: equipment failure,
powering, battery missing, battery failure, battery low, physical
intrusion, self-test failure, dying gasp, temperature yellow and red,
voltage yellow and red, manual power off, invalid image and two power
sourcing overload levels.

A reviewer will meet an older vocabulary in field documentation and in
existing code: LOF, DACT, DIS, MIS, PEE, RDI and others. Those names
come from the transmission convergence layer of the earlier generation,
which this document does not quote, so it defines none of them. Two of
them leave a trace here. Table C.14.2 records that LOBi replaces the
conditions previously known as LOSi and LOFi and both Recommendations
keep LOF in their abbreviation lists, G.988 using the name for alarms of
managed entities unrelated to the PON line.

Performance monitoring is counters on a fifteen minute grid. G.988
clause I.4 names four tools: the PM managed entities, the synchronize
time action on the ONU-G, the threshold data managed entity pair and
the threshold crossing alert. Synchronize time resets every PM
attribute, establishes the tick boundary and starts numbering intervals
from 0 and the interval end time attribute is that number, one byte,
rolling from 255 to 0. The same clause warns that synchronize time is
the only thing guaranteed to reset the phase or the number and that
neither a reboot nor a MIB reset can be expected to do it. In fifteen
minute accumulation mode each entity conceptually has two bins, a
current accumulator and a history bin, which swap roles every fifteen
minutes, with history discarded at age thirty minutes. A get returns the
history bin, the optional get current data action returns the
accumulator and counters saturate rather than wrap. A PM managed entity
holds a pointer to threshold data managed entities, null by default and
leaving it null is how the OLT declines alerts. The OLT creates a
threshold data 1 instance and a threshold data 2 instance as well if
any threshold number exceeds 7. Alerts have edge semantics, unlike the
defects above: clause I.4 raises one when an accumulated value first
equals or exceeds the threshold and issues a second at the end of the
interval that cancels the first, so every interval starts with all
previous alerts explicitly cleared. Alerts travel in OMCI alarm
messages and a class declares either alarms or alerts and never both,
so their code points cannot collide. Message type 16 carries either.

G.9807.1 Table C.14.1 defines the TC layer counters, marks each
mandatory or optional and says which of the ONU, the OLT per ONU and
the OLT as a whole collects it. The groups are PHY, LODS, XGEM,
utilization, PLOAM, activation, OMCI, power monitoring and energy
conservation. Note 1 of
that table repeats the rule of the burst trailer: the BIP-32 error count
gives a
BER estimate only when FEC is off.

GPON and XG-PON deltas
======================

The three systems share a plant and an ONU management protocol and
differ in the transmission convergence layer.

Rates and wavelengths. XGS-PON runs 9.95328 Gbit/s both ways (clause
B.9.2.1). Clause A.8.3 makes the XG-PON combination, 9.95328 Gbit/s
downstream with 2.48832 Gbit/s upstream, a requirement on the OLT only
in the Basic wavelength set scenario, where TDMA coexistence with legacy
XG-PON ONUs is considered. Table C.6.1 lists the combinations an OLT may
support. G.9807.1 gives no rates for GPON, since G.984 defines them.
Clause A.8.2 gives the two wavelength sets described earlier, clause
A.8.4
states that reusing the GPON wavelength means reusing the GPON port of
the WDM1r so the legacy B+ or C+ classes apply. Table C.10.2 encodes
the choice in the least significant bit of the downstream wavelength
channel identifier, 0 basic and 1 optional.

The TC layers. G.988 clause 11.1 calls the systems it manages "GTC
based PON systems" and treats GPON, XG-PON and XGS-PON as one family
with one management protocol. G.9807.1 clause C.6.1.2 defines the
XGS-PON TC layer as three sublayers, service adaptation, framing and PHY
adaptation and clause C.10.1 states that it reuses the concepts of
G.987.3. The differences between XGTC and GTC belong to G.984.3 and
G.987.3 and this document quotes neither.

Encapsulation. Clause C.9.1.2 fixes the XGEM header at 8 bytes. The
older system's GEM header is defined in G.984.3, which this document
does not quote. G.9807.1 nowhere states its size or compares the two, so
no field widths for it appear here.
The one structural remark G.9807.1 makes is in Appendix A.I.2.1: the
XGS-PON TC layer is derived from G.989.3 and also carries G.987.3
compatibility, mainly in the construction of the transmission frame and
the encapsulation of payload datagrams into XGEM fragments.

Alloc-ID width. Clause C.6.1.5.7 makes the XGS-PON Alloc-ID 14 bits with
an assignable range of 1 024 to 16 383. G.984.3 defines the older
system's width and is not quoted here, but G.988 clause 9.2.2 keeps a
trace of the difference: the
unusable initial value of the T-CONT Alloc-ID attribute is 0x00FF or
0xFFFF in G.984 systems and 0xFFFF in every other GTC-based system. Rule
8 of clause C.8.1.1.3 constrains the newer system in a related way: 16
burst allocation series per XGS-PON ONU per BWmap, against 4 for an
XG-PON ONU under G.987.3.

FEC. Clause C.10.1.3 makes FEC support mandatory for both ends in both
directions, statically on downstream and under OLT control upstream.
Clause C.10.1.1.1.3 nonetheless defines a downstream FEC flag in the
operation control structure, then requires the value 1 for XGS-PON.
Whether GPON makes FEC optional is stated in G.984.3, not here.

PLOAM. Clause C.11.2 gives the XGS-PON PLOAM message a fixed 48-byte
layout ending in an 8-byte MIC and clause C.15.6.2 defines that MIC as
a truncated AES-CMAC. The older system's PLOAM message has no equivalent
per message cryptographic check. That statement rests on G.984.3, which
is not quoted here, so treat it as background rather than as standard
text. Two further differences are visible from G.9807.1 itself: clause
C.11.2.1 reserves ONU-ID 0x3FE for a Burst_Profile message addressed to
the 9.95328 Gbit/s population only, because the two populations need
different profiles. Table C.11.6 adds an upstream nominal line rate
indicator to Assign_ONU-ID for a dual rate ONU, while Table C.11.24 adds
an upstream line rate capability bitmap to Serial_Number_ONU. For
discovery, Table C.6.5 keeps three broadcast Alloc-IDs apart and Table
C.6.6 withholds Port-IDs 1 021 and 1 022 from XG-PON ONUs. Clause
C.8.1.1.5 requires the XGS-PON OLT to avoid ending the FS payload with a
short idle XGEM frame, because clause C.9.1.4 records that an XG-PON ONU
reads the FS trailer as a short idle XGEM frame and ignores it.

OMCI. G.988 clause 11.1 states the common ground: all GPON ONUs and OLTs
must support the 48-byte baseline format and both ends use it at
initialization and after every re-ranging, whatever they negotiate
afterwards. The same clause states the format difference that matters:
in G.984 systems the OMCI MIC is a 32-bit cyclic redundancy check and
in later systems it is defined by the TC layer specification. The OMCC
version attribute of the ONU2-G encodes the generation, with clause
9.1.2 assigning code points 0x8y and 0x9y to releases of G.984.4 and
0xAy and 0xBy to releases of G.988. Clause 9.2.1 marks two ANI-G
attributes as meaningful only in the older system: the GEM block length,
the queue occupancy reporting granularity (48 by default) and the
piggyback DBA reporting modes. In every later system the reporting unit
is fixed at 4 bytes and only one reporting mode exists.

Why EPON is a different technology
==================================

EPON is not a variant of the systems above. It is a different technology
that happens to run on the same kind of fiber plant. Neither G.9807.1
nor G.988 specifies it, so nothing in this section carries a clause
reference.

EPON schedules the upstream with a multipoint control protocol, in which
the head end issues gate messages and the subscriber unit answers with
report messages, rather than with a bandwidth map in the downstream
frame header. It carries Ethernet frames without an encapsulation layer
of its own, so it has no counterpart to the XGEM header, the Port-ID or
the LF bit. It manages the subscriber unit with an operations,
administration and maintenance protocol rather than with OMCI, so it has
no managed entity model, no MIB data sync and no class number. A
description covering both would have to abstract away the grant format,
the encapsulation and the management model, which is nearly everything
above, so EPON needs its own treatment.

Further reading
===============

- ITU-T Recommendation G.9807.1 (02/2023), 10-Gigabit-capable symmetric
  passive optical network (XGS-PON).
- ITU-T Recommendation G.988 (11/2022), ONU management and control
  interface (OMCI) specification.

Three further Recommendations are cited by the two above and are worth
knowing by number. ITU-T G.984.3 defines the transmission convergence
layer of GPON. ITU-T G.987.3 defines the transmission convergence layer
of XG-PON. ITU-T G.989.3 defines the transmission convergence layer of
the multi-wavelength system and G.9807.1 borrows several of its clause
structures while marking them not applicable.

The model for this technology is described in
``Documentation/networking/pon.rst``.
