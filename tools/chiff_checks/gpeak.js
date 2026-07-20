const fs=require('fs');
const h=fs.readFileSync('/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/chiff_sim.html','utf8');
const c=h.slice(h.indexOf('const FS = 45000'),h.indexOf('function dialed('));const m={};eval(c+'\nm.render=render;');
const FS=45000,PEAK=(1<<30)-(1<<15),N=512,HOP=64;
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=l){let cr=1,ci=0;for(let k=0;k<l/2;k++){const x=i+k,y=x+l/2;const tr=re[y]*cr-im[y]*ci,ti=re[y]*ci+im[y]*cr;re[y]=re[x]-tr;im[y]=im[x]-ti;re[x]+=tr;im[x]+=ti;const q=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=q;}}}}
function gpeak(a){const p={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:a,seed:9,chiffDur:600};
  const r=m.render(p),d=m.render({...p,amount:0}).out;let g=1e-12;
  for(let s=0;s+N<r.totalN;s+=HOP){const re=new Float64Array(N),im=new Float64Array(N);
    for(let k=0;k<N;k++){const w=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));re[k]=(r.out[s+k]-d[s+k])/PEAK*w;}
    fft(re,im);for(let b=0;b<N/2;b++){const mg=Math.hypot(re[b],im[b]);if(mg>g)g=mg;}}
  return g;}
const ref=gpeak(127);
console.log('gPeak per amount, and dB relative to amount=127 (the fixed-ref target):');
for(const a of [0,1,4,16,64,127]){const g=gpeak(a);console.log(`  amt=${String(a).padStart(3)}: gPeak=${g.toExponential(2)}  ${(20*Math.log10(g/ref)).toFixed(1)} dB`);}
console.log(`\nsuggested fixed REF = amount127 gPeak = ${ref.toExponential(3)}`);
