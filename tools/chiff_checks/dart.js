// Dart chiff model (float, for iterating on behavior):
//  value slews toward the STAGE TARGET at the stage's nominal (slow) rate -> the
//  classic ADSR envelope = the mean. With prob p per sample it instead slews
//  toward a RAIL at the (ramping) fast chiff rate -> an excursion, then relaxes.
//  Burst decays because the fast rate ramps back to nominal over chiffDur.
const FS=45000;
function shiftOf(nSamp){ return Math.max(0, Math.log2(Math.max(1,nSamp)) - 2); } // ~log2(N/4)
function alpha(sh){ return Math.pow(2,-sh); }
// rail: 'down' = toward 0 only; 'both' = toward nearer-balancing rail
function render(P){
  const aN=P.atk*FS/1000, dN=P.dec*FS/1000, rN=P.rel*FS/1000, gN=P.gate*FS/1000;
  const chN=P.chiffDur*FS/1000, peak=P.peak, sus=P.peak*P.sustain;
  const shA=shiftOf(aN), shD=shiftOf(dN), shR=shiftOf(rN);
  const p=P.amount/127*0.5;                 // dart probability
  const shDrop=1.0;                         // fast chiff shift at burst start
  let value=0, dialed=0, chiffLeft=chN, t=0;
  let seed=P.seed||12345; const rnd=()=>{seed=(seed*1103515245+12345)&0x7fffffff;return seed/0x7fffffff;};
  const out=[], mean=[];
  const total=Math.round(gN+rN+0.02*FS);
  for(let i=0;i<total;i++){
    // stage
    let stgTarget, shNom;
    if(i<aN){stgTarget=peak;shNom=shA;}
    else if(i<gN){ if(i<aN+dN){stgTarget=sus;shNom=shD;} else {stgTarget=sus;shNom=shD;} }
    else {stgTarget=0;shNom=shR;}
    // dialed (chiff-free reference): slew toward stage target at nominal
    dialed += (stgTarget-dialed)*alpha(shNom);
    // chiff fast rate: ramps from shDrop up to shNom over chiffDur
    const frac = chiffLeft>0 ? chiffLeft/chN : 0;
    const shChiff = shNom + (shDrop - shNom)*frac;   // dropped early, nominal late
    const isDart = chiffLeft>0 && rnd()<p;
    if(isDart){
      let rail = P.rail==='both' ? (rnd()<value/peak?0:peak) : 0;  // 'both' balances by level
      value += (rail-value)*alpha(shChiff);
    } else {
      value += (stgTarget-value)*alpha(shNom);
    }
    if(chiffLeft>0) chiffLeft--;
    out.push(value); mean.push(dialed);
  }
  return {out,mean,gN,aN,dN,rN,total};
}
function stats(P,label){
  const r=render(P);
  // noise = rms(value - dialed); mean error; per stage
  function seg(a,b){let e=0,n=0,me=0;for(let i=a;i<b&&i<r.total;i++){const d=r.out[i]-r.mean[i];e+=d*d;me+=r.out[i]-r.mean[i];n++;}return{rms:Math.sqrt(e/n),bias:me/n};}
  const A=seg(0,r.aN), D=seg(r.aN,r.aN+r.dN), Srg=seg(r.aN+r.dN,r.gN), R=seg(r.gN,r.gN+r.rN);
  console.log(`${label}`);
  console.log(`  attack  noise=${(A.rms*100).toFixed(2)}%  meanbias=${(A.bias*100).toFixed(2)}%`);
  console.log(`  decay   noise=${(D.rms*100).toFixed(2)}%  meanbias=${(D.bias*100).toFixed(2)}%`);
  console.log(`  sustain noise=${(Srg.rms*100).toFixed(2)}%  meanbias=${(Srg.bias*100).toFixed(2)}%`);
  console.log(`  release noise=${(R.rms*100).toFixed(2)}%  meanbias=${(R.bias*100).toFixed(2)}%`);
}
const base={atk:150,dec:150,rel:300,peak:0.8,sustain:0.5,gate:800,chiffDur:900,amount:96,rail:'down'};
console.log('=== delta-insensitivity: decay noise should NOT depend on sustain level ===');
stats({...base,sustain:0.2},'sustain=0.2 (big decay delta):');
stats({...base,sustain:0.7},'sustain=0.7 (small decay delta):');
