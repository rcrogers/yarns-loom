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
// 2. basic chiff: noise at onset, gone by ~window end (dur 90 ~ 580ms), mean near dialed
{ const s=run('basic 96 90'), d=run('basic 0 90');
  const n0=noiseWin(s,0,100), n1=noiseWin(s,300,500), n2=noiseWin(s,700,1100);
  check('onset noise present', n0>50, n0.toFixed(1));
  check('noise fades by window end', n2<n0/50, n2.toFixed(2)+' vs onset '+n0.toFixed(1));
  const bias=(meanWin(s,700,1100)-meanWin(d,700,1100))/FS_OUT*100;
  check('post-window mean == dialed', Math.abs(bias)<1, bias.toFixed(2)+'%');
  const biasLoud=(meanWin(s,100,300)-meanWin(d,100,300))/FS_OUT*100;
  check('loud-phase dip bounded', biasLoud>-40 && biasLoud<5, biasLoud.toFixed(1)+'%');
  let mx=0; for(const v of s) if(v>mx)mx=v;
  let dmx=0; for(const v of d) if(v>dmx)dmx=v;
  
  check('never exceeds note top rail', mx<=16384, mx+' (rail 16384, classic max '+dmx+')');
}
// 3. early release: noise continues into release, lands ~0 by release end (400ms)
{ const s=run('early_release 96 127');  // long chiff forces compression
  const nStart=noiseWin(s,60,160), nEnd=noiseWin(s,420,460), nAfter=noiseWin(s,480,600);
  check('release keeps noise', nStart>30, nStart.toFixed(1));
  check('noise lands by release end', nEnd<nStart/20, nEnd.toFixed(2));
  check('silence after release', nAfter<0.5, nAfter.toFixed(2));
}
// 4. retrigger during release: no full-scale transient
{ const s=run('retrigger 96 90');
  let mx=0; const a=600*45-45, b=602*45; // around the retrigger at 600ms
  for(let i=a;i<b;i++) mx=Math.max(mx,Math.abs(s[i]-s[i-1]));
  check('retrigger transient bounded', mx<FS_OUT/4, 'max step '+mx);
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
{ const s=run('inverted 96 90'), d=run('inverted 0 90');
  const n0=noiseWin(s,0,100), n2=noiseWin(s,700,1100);
  check('inverted chiff onset noise', n0>50, n0.toFixed(1));
  check('inverted noise fades', n2<n0/50, n2.toFixed(2));
  const bias=(meanWin(s,700,1100)-meanWin(d,700,1100))/FS_OUT*100;
  check('inverted post-window mean == dialed', Math.abs(bias)<1, bias.toFixed(2)+'%');
  let flat=0,run_=0; for(let i=45*100;i<45*1000;i++){ if(s[i]===s[i-1]){run_++;flat=Math.max(flat,run_);} else run_=0; }
  check('inverted chiff no pinning plateaus', flat<500, 'longest flat '+flat);
}
// 7. Late-window early release must not hang (restructure regression trap)
{ const s=run('latehang 96 127');
  const relStart=s[7000*45-1], relEnd=s[Math.min(7100*45, s.length-1)];
  check('latehang release falls', relEnd < relStart*0.15,
    (relStart/FS_OUT*100).toFixed(1)+'% -> '+(relEnd/FS_OUT*100).toFixed(1)+'%');
}
// 8. No kink at window end (basic: window ends mid-attack at ~580ms)
{ const s=run('basic 96 90'), d=run('basic 0 90');
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
