// Dart v3: darts aim at dialed*(1±k), clamped to [0,max]. q=0.5 (alternate) so
// mean stays on dialed with no q(1-q) vanish; excursion = k*dialed tracks the
// level (quiet on quiet notes); clamp = no clip; burst fades as rate->nominal.
const FS=45000;
const shiftOf=n=>Math.max(0,Math.log2(Math.max(1,n))-2), alpha=s=>Math.pow(2,-s);
function render(P){
  const aN=P.atk*FS/1000,dN=P.dec*FS/1000,rN=P.rel*FS/1000,gN=P.gate*FS/1000,chN=P.chiffDur*FS/1000;
  const peak=P.peak, sus=P.peak*P.sustain, p=0.5, k=P.amount/127*0.9, shDrop=1.0, max=peak;
  let value=0,dialed=0,chiffLeft=chN, seed=P.seed||123, up=false;
  const rnd=()=>{seed=(seed*1103515245+12345)&0x7fffffff;return seed/0x7fffffff;};
  const out=[],mean=[], total=Math.round(gN+rN+0.02*FS);
  for(let i=0;i<total;i++){
    let stgTarget,shNom;
    if(i<aN){stgTarget=peak;shNom=shiftOf(aN);}
    else if(i<gN){stgTarget=sus;shNom=shiftOf(dN);}
    else {stgTarget=0;shNom=shiftOf(rN);}
    dialed += (stgTarget-dialed)*alpha(shNom);
    const frac=chiffLeft>0?chiffLeft/chN:0, shChiff=shNom+(shDrop-shNom)*frac;
    let tgt=dialed;
    if(chiffLeft>0 && rnd()<p){ up=!up; tgt = up?Math.min(max,dialed*(1+k)):Math.max(0,dialed*(1-k)); }
    value += (tgt-value)*alpha(shChiff);
    if(chiffLeft>0)chiffLeft--;
    out.push(value);mean.push(dialed);
  }
  return {out,mean,gN,aN,dN,rN,total};
}
function stats(P,label){
  const r=render(P);
  const seg=(a,b)=>{let e=0,me=0,n=0;for(let i=a;i<b&&i<r.total;i++){const d=r.out[i]-r.mean[i];e+=d*d;me+=d;n++;}return{rms:Math.sqrt(e/n),bias:me/n};};
  const A=seg(0,r.aN),D=seg(r.aN,r.aN+r.dN),S=seg(r.aN+r.dN,r.gN),R=seg(r.gN,r.gN+r.rN);
  console.log(`${label} atk n=${(A.rms*100).toFixed(1)}/b=${(A.bias*100).toFixed(1)} dec n=${(D.rms*100).toFixed(1)}/b=${(D.bias*100).toFixed(1)} sus n=${(S.rms*100).toFixed(1)}/b=${(S.bias*100).toFixed(1)} rel n=${(R.rms*100).toFixed(1)}/b=${(R.bias*100).toFixed(1)}`);
}
const base={atk:150,dec:150,rel:300,peak:0.8,gate:800,chiffDur:900,amount:96};
console.log('n=noise%  b=meanbias%   (want: monotonic n atk>=dec>=sus>=rel, small b)');
stats({...base,sustain:0.2},'sus=0.2');
stats({...base,sustain:0.5},'sus=0.5');
stats({...base,sustain:0.7},'sus=0.7');
