const fs=require('fs');
function LR(f){const h=fs.readFileSync(f,'utf8');const c=h.slice(h.indexOf('const FS = 45000'),h.indexOf('function dialed('));const m={};eval(c+'\nm.render=render;');return m.render;}
const FS=45000,PEAK=(1<<30)-(1<<15);
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=l){let cr=1,ci=0;for(let k=0;k<l/2;k++){const x=i+k,y=x+l/2;const tr=re[y]*cr-im[y]*ci,ti=re[y]*ci+im[y]*cr;re[y]=re[x]-tr;im[y]=im[x]-ti;re[x]+=tr;im[x]+=ti;const q=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=q;}}}}
function cen(r,d,t){const N=512,s=t*45,re=new Float64Array(N),im=new Float64Array(N);for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));re[k]=(r.out[s+k]-d[s+k])/PEAK*h;}fft(re,im);let nu=0,de=0;for(let b=2;b<N/2;b++){const m=Math.hypot(re[b],im[b]);nu+=b/N*FS*m;de+=m;}return de>1e-9?nu/de/1000:0;}
const dart=LR('/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/chiff_sim.html');
const dp={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:96,seed:9,chiffDur:600};
console.log('GOOD reference centroid: onset ~8kHz -> tail ~5.6kHz (falls ~30%)');
console.log('dart centroid trajectory (kHz) at t=25/100/200/350ms, for shDark:');
for(const sd of [3,5,7,9,12]){const r=dart({...dp,_shDark:sd}),d=dart({...dp,_shDark:sd,amount:0}).out;
  console.log(`  shDark=${String(sd).padStart(2)}: ${[25,100,200,350].map(t=>cen(r,d,t).toFixed(1)).join(' -> ')}`);}
