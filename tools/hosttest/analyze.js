// Firmware dart-port checks against sim-established expectations.
const { execSync } = require('child_process');
function run(args){ return execSync('./test '+args,{maxBuffer:1e9}).toString().trim().split('\n').map(Number); }
function noiseWin(s,aMs,bMs){ let sum=0,n=0;
  for(let i=Math.max(1,aMs*45);i<Math.min(bMs*45,s.length);i++){sum+=Math.abs(s[i]-s[i-1]);n++;}
  return n?sum/n:0; }
function meanWin(s,aMs,bMs){ let m=0,n=0;
  for(let i=aMs*45;i<Math.min(bMs*45,s.length);i++){m+=s[i];n++;} return n?m/n:0; }
const FS_OUT = 32767;
let fails = 0;
function check(name,cond,detail){ console.log((cond?'PASS':'FAIL')+' '+name+(detail?'  ['+detail+']':'')); if(!cond)fails++; }

// 1. amount 0 = classic: monotone rising attack, no noise anywhere
{ const s=run('basic 0 90');
  let steps=0; for(let i=1;i<45*1100;i++) if(s[i]<s[i-1]) steps++;
  check('amt0 attack monotone', steps===0, steps+' down-steps');
  check('amt0 no noise', noiseWin(s,1300,1900)<1, noiseWin(s,1300,1900).toFixed(2));
}
// 2. basic chiff: noise at onset, gone by ~window end, mean near the nominal value. The
// window is now attack-relative; attack=249ms makes dur 90 (2.33x attack) land
// at ~580ms, so the fixed measurement windows below still bracket it.
{ const s=run('basic 96 90 attack=249'), d=run('basic 0 90 attack=249');
  const n0=noiseWin(s,0,100), n1=noiseWin(s,300,500), n2=noiseWin(s,700,1100);
  check('onset noise present', n0>50, n0.toFixed(1));
  check('noise fades by window end', n2<n0/50, n2.toFixed(2)+' vs onset '+n0.toFixed(1));
  const bias=(meanWin(s,700,1100)-meanWin(d,700,1100))/FS_OUT*100;
  check('post-window mean == nominal', Math.abs(bias)<1, bias.toFixed(2)+'%');
  const biasLoud=(meanWin(s,100,300)-meanWin(d,100,300))/FS_OUT*100;
  check('loud-phase dip bounded', biasLoud>-40 && biasLoud<5, biasLoud.toFixed(1)+'%');
  let mx=0; for(const v of s) if(v>mx)mx=v;
  let dmx=0; for(const v of d) if(v>dmx)dmx=v;
  
  // The state clamp bounds the DAC range now, not the note's: bias is folded
  // into the render state so ONE usat serves both the integrator and the
  // output. A chiff can therefore push the envelope above its dialled peak --
  // deliberately, and the user's call. What must still hold is that it stays
  // inside the DAC and that the overshoot is a transient's worth, not a
  // different level: worst MEASURED is +10.9% at AMOUNT 127 with high sustain.
  const over=(mx-16383)*100/16383;
  check('stays inside the DAC range', mx<=32767, mx+'');
  check('peak overshoot is bounded', over<15,
        over.toFixed(1)+'% above note top (classic max '+dmx+')');
}
// 3. early release: noise continues into release, lands ~0 by release end (400ms)
{ const s=run('early_release 96 127');  // long chiff forces compression
  const nStart=noiseWin(s,60,160), nEnd=noiseWin(s,420,460), nAfter=noiseWin(s,480,600);
  check('release keeps noise', nStart>30, nStart.toFixed(1));
  check('noise lands by release end', nEnd<nStart/20, nEnd.toFixed(2));
  check('silence after release', nAfter<0.5, nAfter.toFixed(2));
}
// 4. retrigger during release: the transient must be in family with the
// chiff's own motion, not a discontinuity. Compared against the trace's own
// steps rather than an absolute constant -- the old absolute limit (FS_OUT/4)
// sat BELOW the design's normal max step (10015 measured elsewhere in the same
// trace), so it was passing on luck and failed the moment the chiff got denser.
{ const s=run('retrigger 96 90');
  let mx=0; const a=600*45-45, b=602*45; // around the retrigger at 600ms
  for(let i=a;i<b;i++) mx=Math.max(mx,Math.abs(s[i]-s[i-1]));
  const elsewhere=[];
  for(let i=1;i<s.length;i++) if(i<a-45||i>=b+45) elsewhere.push(Math.abs(s[i]-s[i-1]));
  elsewhere.sort((x,y)=>x-y);
  const worstNormal=elsewhere[elsewhere.length-1];
  check('retrigger transient in family with chiff motion', mx<=worstNormal*1.1,
    'retrigger step '+mx+' vs worst step elsewhere '+worstNormal);
}
// 5. held note: sustain has motion while window lives (dur 127 = 8s)
{ const s=run('held 96 127');
  const nSus=noiseWin(s,2000,3000), nLate=noiseWin(s,8200,8900);
  check('sustain has noise (8s chiff)', nSus>5, nSus.toFixed(1));
  check('noise closes by 8s', nLate<nSus/5, nLate.toFixed(2));
}
console.log(fails ? fails+' FAILURES' : 'ALL PASS');
// 6. INVERTED range (CV DAC / negative timbre): the hardware-breaking case
{ const s=run('inverted 0 90');
  let up=0; for(let i=64;i<45*1100;i++) if(s[i]>s[i-1]) up++;
  check('inverted amt0 attack monotone down', up===0, up+' up-steps');
  let flat=0,run_=0; for(let i=45*100;i<45*1000;i++){ if(s[i]===s[i-1]){run_++;flat=Math.max(flat,run_);} else run_=0; }
  check('inverted amt0 no long plateaus', flat<200, 'longest flat '+flat+' samples');
}
{ const s=run('inverted 96 90 attack=249'), d=run('inverted 0 90 attack=249');
  const n0=noiseWin(s,0,100), n2=noiseWin(s,700,1100);
  check('inverted chiff onset noise', n0>50, n0.toFixed(1));
  check('inverted noise fades', n2<n0/50, n2.toFixed(2));
  const bias=(meanWin(s,700,1100)-meanWin(d,700,1100))/FS_OUT*100;
  check('inverted post-window mean == nominal', Math.abs(bias)<1, bias.toFixed(2)+'%');
  // Pinning check wants a slow (default) attack so 100-1000ms is still the
  // rising attack -- a short attack would reach a flat sustain there and this
  // measures dwell, not attack rate.
  const sp=run('inverted 96 90');
  let flat=0,run_=0; for(let i=45*100;i<45*1000;i++){ if(sp[i]===sp[i-1]){run_++;flat=Math.max(flat,run_);} else run_=0; }
  check('inverted chiff no pinning plateaus', flat<500, 'longest flat '+flat);
}
// 7. Late-window early release must not hang (restructure regression trap)
{ const s=run('latehang 96 127');
  const relStart=s[7000*45-1], relEnd=s[Math.min(7100*45, s.length-1)];
  check('latehang release falls', relEnd < relStart*0.15,
    (relStart/FS_OUT*100).toFixed(1)+'% -> '+(relEnd/FS_OUT*100).toFixed(1)+'%');
}
// 8. No kink at window end (attack=249 -> attack-relative window ~= 580ms)
{ const s=run('basic 96 90 attack=249'), d=run('basic 0 90 attack=249');
  let worst=0;
  for(let w=590;w<730;w+=10){ let m=0,n=0;
    for(let i=w*45;i<(w+10)*45;i++){m+=s[i]-d[i];n++;}
    worst=Math.max(worst,Math.abs(m/n)); }
  check('no kink at window end', worst<FS_OUT*0.01, 'worst gap '+(worst/FS_OUT*100).toFixed(2)+'%');
}
// 9. amount continuity ladder: 0 -> 1 step must not exceed later steps
{ const ladder=[0,1,2,4,8].map(a=>{
    const s=run('basic '+a+' 90');
    let sum=0,n=0;
    for(let i=20*45;i<120*45;i++){sum+=Math.abs(s[i]-s[i-1]);n++;}
    return sum/n;
  });
  const step01=ladder[1]-ladder[0], step48=ladder[4]-ladder[3];
  check('amount 0->1 continuous', step01 < Math.max(1,step48*2),
    'steps 0->1: '+step01.toFixed(2)+' vs 4->8: '+step48.toFixed(2));
}

// BIAS PATH. Everything above renders with bias == 0, which for a long time
// meant the bias arithmetic was never executed by any check -- including the
// QEMU asm-vs-C differential. That matters most for the unified clamp, whose
// whole mechanism is that the render state carries envelope PLUS bias: with
// bias 0 the state is just the value, both ramp adds add nothing, and the
// recovery after the loop subtracts nothing. `tremolo=` drives the bias the
// way Oscillator::Render does -- target sampled per block from the envelope's
// own value, then ramped toward it -- so the folding, the ramp and the
// recovery all run.
{ const s=run('basic 0 90 attack_setting=40 decay_setting=64 sustain_setting=70 '+
              'release_setting=64 gate=2000 tail=1400 tremolo=24000');
  let mn=32767, mx=0; for(const v of s){ if(v<mn)mn=v; if(v>mx)mx=v; }
  check('bias path: output inside the DAC range', mn>=0 && mx<=32767, mn+'..'+mx);
  // A recovery that is off by even a little would accumulate once per run and
  // show up as the sustain walking; flat here means value = state - bias is
  // exact across run boundaries.
  const a=meanWin(s,1500,1600), b=meanWin(s,1900,2000);
  check('bias path: sustain does not drift', Math.abs(a-b)<8,
        'sustain '+a.toFixed(1)+' -> '+b.toFixed(1));
  // Release setting 64 is 795 ms, so the note is not done until ~2795. What is
  // left after that is NOT a bias artifact: a stage lands at 1 - e^-4 of its
  // span and the rest is shed slowly in DEAD, so a residual proportional to
  // the sustain level is expected (open item 6 in the plan is about removing
  // it). MEASURED: 53 without tremolo, 33 with -- bias makes it SMALLER, so
  // the check is against the sustain level, not against zero.
  const tail=meanWin(s,3000,3100);
  check('bias path: post-release residual is small vs sustain', tail < a*0.02,
        tail.toFixed(1)+' vs sustain '+a.toFixed(0));
}

// THE CLIP PATH. Tremolo above cannot reach it: it is negative feedback scaled
// to the envelope's own value, so envelope + bias stays near range however deep
// it is set. A timbre-LFO-style bias is INDEPENDENT of the envelope, so the sum
// leaves the DAC range and the clamp has to bite -- which is the whole point of
// folding bias into the render state. MEASURED at bias_lfo 20000: 3732 samples
// on the ceiling, 40546 on the floor.
{ const args='basic 0 90 attack_setting=40 decay_setting=64 sustain_setting=70 '+
             'release_setting=64 gate=1200 tail=800 bias_lfo=20000 bias_lfo_blocks=8';
  const s=run(args);
  let mn=99999, mx=-99999, hi=0, run_=0, worst=0;
  for(const v of s){ if(v<mn)mn=v; if(v>mx)mx=v;
    if(v>=32767){ hi++; run_++; if(run_>worst)worst=run_; } else run_=0; }
  check('clip path: never leaves the DAC range', mn>=0 && mx<=32767, mn+'..'+mx);
  check('clip path: the clamp actually bites', hi>100, hi+' samples on the ceiling');
  // Anti-windup: the state is clamped, so when the bias reverses the output
  // must come off the rail with the bias. The LFO holds each polarity for 8
  // blocks = 512 samples, so a dwell much beyond that means the state wound up
  // behind a saturated output and has to unwind before anything moves -- the
  // failure the old output-only usat could not prevent, because it did not
  // feed back.
  check('clip path: no windup behind the rail', worst <= 640,
        'longest ceiling dwell '+worst+' samples (bias holds 512)');
}

// THE INVARIANT, and it is the strongest check in this file: the output is
// saturate(envelope + bias), where the envelope is bit-for-bit what it would
// have been with bias 0. Equivalently, the envelope's OWN trajectory does not
// depend on the bias. Read value_q30_ per block and diff against a bias-0 run
// at the same settings.
//
// EVERY OTHER CHECK HERE READS THE OUTPUT SAMPLES, which stay in range and look
// correct even while the clamp is writing bias back into the envelope. That is
// why this class went unseen: with the render state carrying envelope + bias,
// the state clamp bounded the SUM, so at a rail it clipped the bias into the
// envelope's own integrator. MEASURED then, against a bias-0 run: the value
// diverged by EXACTLY the bias amplitude (10, 8000, 20000 s16) every time the
// sum touched a rail, and the battery was all green.
{ const base='basic 96 90 value_trace=1 gate=2000 tail=1000 range=32767 ';
  const ref=run(base+'bias_lfo=0');
  for (const bias of [10, 8000, 20000, 32767]) {
    const s=run(base+'bias_lfo='+bias);
    let worst=0;
    for (let i=0;i<ref.length;i++){ const d=Math.abs(s[i]-ref[i]); if(d>worst)worst=d; }
    // EXACT, not within a tolerance: bias reaches the buffer on a scratch copy
    // and is never fed back, so there is no path -- not even a rounding one --
    // from bias into the envelope. A tolerance here would hide the defect this
    // check exists for, which was worth thousands of Q30 LSBs.
    check('invariant: envelope is bias-independent (bias '+bias+')', worst===0,
          'max |diff| '+(worst/32768).toFixed(4)+' s16 over '+ref.length+' blocks');
  }
}

// NEGATIVE ENVELOPES. A negative TIMBRE MOD ENV makes part.cc's timbre_14
// negative (it is CONSTRAINed to [-8192, 8191]), so WarpTimbre's target is
// negative and timbre_envelope_.NoteOn gets min 0 / max NEGATIVE -- the note's
// whole range sits BELOW zero. The base timbre arrives as the envelope's BIAS,
// and the envelope is supposed to SUBTRACT from it.
//
// 6a8a00b8 moved the value clamp from the note's range to [0, 2^30) and killed
// this outright: MEASURED at that bound, value range 0..0 and the output flat
// at the bias -- the envelope did not move at all, silently. Nothing in this
// file could see it, because every scenario had a non-negative floor.
{ const args='basic 0 90 range=-16383 bias_lfo=20000 bias_lfo_blocks=1000000 '+
             'gate=2000 tail=500 attack_setting=40';
  const rng=execSync('./test '+args+' value_range=1',{maxBuffer:1e9})
    .toString().trim().split(/\s+/).map(Number);
  // The envelope must actually travel to its negative target, not sit pinned.
  check('negative range: the envelope reaches its target',
        rng[0] < -15000, 'value range '+rng[0]+'..'+rng[1]);
  const s=run(args);
  // MEASURE AFTER THE BIAS HAS RAMPED IN. Sample 0 is ~300 whatever the
  // envelope does, because the bias slews up from 0 across the first block --
  // a min over the whole render reads that startup transient and passes even
  // when the envelope is dead. Caught by mutation-testing this check against
  // the broken build, which it passed at "output dips to 312".
  // Loop, not Math.min.apply: the render is ~112k samples and apply() spreads
  // as arguments, which overflows the stack and silently kills the rest of the
  // file (RangeError inside execSync's caller).
  const dip=meanWin(s,120,150);
  let mn=99999, mx=-99999; for(const v of s){ if(v<mn)mn=v; if(v>mx)mx=v; }
  // With a standing bias of 20000 the envelope must pull the output DOWN. The
  // threshold is HALF the bias: working it reaches ~4000, dead it sits at
  // 20000, so neither verdict is near the line. 120-150 ms is the trough --
  // the attack has travelled but the release has not started.
  check('negative range: the envelope subtracts from the bias', dip < 10000,
        'output at 120-150ms is '+dip.toFixed(0)+' of a 20000 bias'+
        ' (20000 means the envelope is dead)');
  check('negative range: output still inside the DAC range',
        mn>=0 && mx<=FS_OUT, mn+'..'+mx);
}

// The invariant again, on a range that sits BELOW zero: the clamp offset must
// not become a second path from bias into the envelope.
{ const base='basic 96 90 range=-16383 gate=2000 tail=500 value_trace=1 ';
  const ref=run(base+'bias_lfo=0');
  for (const bias of [10, 20000]) {
    const s=run(base+'bias_lfo='+bias);
    let worst=0;
    for (let i=0;i<ref.length;i++){ const d=Math.abs(s[i]-ref[i]); if(d>worst)worst=d; }
    check('negative range: envelope is bias-independent (bias '+bias+')',
          worst===0, 'max |diff| '+(worst/32768).toFixed(4)+' s16');
  }
}

// The recovered value must stay inside the DAC range, because tremolo() forms
// (value - release target) * strength_u16 in int32. Every release target is
// non-negative, so a bounded value keeps |relative| <= 32767 and the product at
// 32767 * 65535 = 2147385345, which fits with 98302 to spare. Unbounded it does
// not: MEASURED 36063 before the split, i.e. 2.36e9, an overflow -- and
// value() returns int16_t, so 36063 wrapped there too.
{ const range=(args)=>execSync('./test '+args,{maxBuffer:1e9})
    .toString().trim().split(/\s+/).map(Number);
  for (const args of ['bias_lfo=32767', 'bias_lfo=32767 tremolo=48000']) {
    const r=range('basic 96 90 value_range=1 range=32767 '+args);
    check('value stays in the DAC range, so tremolo cannot overflow ('+args+')',
          r[0]>=0 && r[1]<=FS_OUT, r[0]+'..'+r[1]);
  }
}
