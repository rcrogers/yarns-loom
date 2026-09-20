(* ::Package:: *)

(* AUDIO-RATE PWM, as the render actually computes it.

   PHASE IS IN TURNS. One turn is one full cycle of the phase accumulator:
   2^32 in the fixed-point render, 1 here, 2 Pi radians. 0x80000000 is half a
   turn, which is why it is the 50% duty the swing is centred on.

   A is the swing amplitude IN TURNS. A = 1/2 means the width reaches 0% and
   100% duty; A > 1/2 means it wraps past a whole turn, which is where the
   pulse births -- and the cliffs -- come from.

   In the render, sine and timbre are both 15-bit, so their product reaches
   2^30 = a quarter turn, and the per-ratio upshift k gives A = 2^(k-2) turns. *)

saw[x_] := Mod[x, 1];

(* The pulse the render emits. Two saws, one offset: the difference is
   Mod[w,1] on the high part and Mod[w,1]-1 on the low, so it is two-level for
   ANY w -- nothing about it is bounded by a period. *)
pulse[phi_, w_] := saw[phi] - saw[phi - w];

phi[t_] := fc t;
w[t_] := 1/2 + A Sin[2 Pi fm t];
offset[t_] := phi[t] - w[t];        (* the second saw's phase *)
r := fm/fc;                          (* modulator : carrier *)

(* ---------------------------------------------------------------- edges *)
(* Output edges are the carrier's own wrap, once per period, plus every
   crossing of `offset` through an integer -- each of those is a wrap of the
   offset saw. *)
offsetRate[t_] := D[offset[t], t] // Evaluate    (* fc - 2 Pi A fm Cos[2 Pi fm t] *)

(* MONOTONE: offset never turns around, so it crosses each integer once and
   the edge count is fixed. This is classic PWM -- the edges MOVE, none are
   born, and nothing pops. *)
monotone = Reduce[2 Pi A fm < fc && A > 0 && fc > 0 && fm > 0, A];
(*  ==>  A < 1/(2 Pi r)  *)

(* WRAPPING: the peak-to-peak swing exceeds a turn, so the duty passes through
   0% or 100% and a pulse that did not exist appears. This is the +5 to +6 dB
   cliff, and it tracks 2A alone -- not the ratio. *)
wrapping = 2 A > 1;

(* Edges per carrier period, counted rather than derived. *)
edgeCount[aa_?NumericQ, rr_?NumericQ, nPeriods_: 64] :=
  Module[{f = 1, m = rr, d, ts, vals},
    d[t_] := t - (1/2 + aa Sin[2 Pi m t]);
    ts = Subdivide[0, nPeriods, 200 nPeriods];
    vals = Floor /@ (d /@ ts);
    (* one carrier wrap per period, plus every integer crossing of offset *)
    (nPeriods + Count[Differences[vals], x_ /; x != 0]) / nPeriods];

(* --------------------------------------------- why a birth is a CLIFF *)
(* Near the sine's peak the width turns around QUADRATICALLY, so a boundary B
   it has only just reached is crossed TANGENTIALLY: the two crossings separate
   as Sqrt of how far past threshold the depth is. Square root means infinite
   slope at the birth, which is why the new harmonics arrive as a step rather
   than a fade. *)
peakExpansion = Normal[Series[w[t], {t, 1/(4 fm), 2}]];
birthSeparation = tau /. Solve[
    (1/2 + A - 2 A Pi^2 fm^2 tau^2) == B, tau];
(*  tau = +- Sqrt[(1/2 + A - B)/(2 A Pi^2 fm^2)]  ~  Sqrt[A - Acrit]  *)

(* --------------------------------------------------- the render's bound *)
(* The offset saw's per-sample motion is read as a SIGNED 32-bit difference,
   so it carries a direction only while it stays under half a turn per sample.
   That is Nyquist on that saw, and it is the note's business, not the ratio's. *)
offsetFreq[t_] := fc - 2 Pi A fm Cos[2 Pi fm t];
nyquistBound = Reduce[fc + 2 Pi A fm < fs/2 && A > 0, A];
(*  ==>  A < (fs/2 - fc)/(2 Pi fm)   -- at MIDI 48 and r = 1 this is ~27 turns,
    which is why reading the swing as a bounded WIDTH cost a factor of eight. *)

(* ------------------------------------------------------------ multipulse *)
(* Deep AND smooth, by construction rather than by overdriving. A FIXED number
   of edges per period, at phases 0, w1, w2, ... -- each offset staying inside
   its own turn, so the count never changes and no pulse is ever born.
   The list must be EVEN: the alternating signs are what cancel the saw ramps
   and leave a piecewise-constant waveform. *)
multiPulse[phi_, offsets_List] /; EvenQ[Length[offsets]] :=
  Sum[(-1)^(k - 1) saw[phi - offsets[[k]]], {k, 1, Length[offsets]}];

(* Four edges, two independently modulated widths, no births at any depth: *)
fourEdge[t_] := multiPulse[fc t, {0, w1[t], w2[t], w3[t]}];

(* ------------------------------------------------------------- readings *)
(* What the sweeps measured, for checking against the audio:
     edgeCount[4,    0.785]   pi/4 at the committed depth   -- wraps, cliffs
     edgeCount[0.5,  0.785]   pi/4 capped at one turn       -- no wrap
     monotone /. {fm -> 2 fc}                               -- A < 1/(4 Pi)
     2 A > 1                                                -- the cliff test *)
