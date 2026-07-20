const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
function P(sustain){return{attack:150,decay:150,peak:0.8,sustain,release:300,gate:800,
  amount:96,chiffDur:900,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4,dutySeed:0,seed:0xCAFEBABE};}
function P0(x){return{...P(x.sustain),amount:0};}
function stats(sustain){
  const r=mod.render(P(sustain)); const d=mod.render(P0({sustain})).out;
  const seg=(a,b)=>{let e=0,me=0,n=0;for(let i=a;i<b&&i<r.totalN;i++){const x=r.out[i]-d[i];e+=x*x;me+=x;n++;}return{n:Math.sqrt(e/n)/PEAK*100,b:me/n/PEAK*100};};
  const g=r.gateN, aN=r.aN, dN=r.dN;
  const A=seg(0,aN),D=seg(aN,aN+dN),S=seg(aN+dN,g),R=seg(g,g+r.rN);
  let bad=false; for(let i=0;i<r.totalN;i++) if(!isFinite(r.out[i])||r.out[i]<-PEAK||r.out[i]>2*PEAK) bad=true;
  console.log(`sus=${sustain}: atk n=${A.n.toFixed(1)}/b=${A.b.toFixed(1)} dec n=${D.n.toFixed(1)}/b=${D.b.toFixed(1)} sus n=${S.n.toFixed(1)}/b=${S.b.toFixed(1)} rel n=${R.n.toFixed(1)}/b=${R.b.toFixed(1)} ${bad?'*** BLOWUP':'ok'}`);
}
console.log('n=noise% (vs chiff-free), b=meanbias%. delta test: decay noise ~same across sustain:');
stats(0.2); stats(0.5); stats(0.7);
