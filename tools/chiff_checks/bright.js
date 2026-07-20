const fs=require('fs');
function LR(f){const h=fs.readFileSync(f,'utf8');const c=h.slice(h.indexOf('const FS = 45000'),h.indexOf('function dialed('));const m={};eval(c+'\nm.render=render;');return m.render;}
const FS=45000,PEAK=(1<<30)-(1<<15);
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=l){let cr=1,ci=0;for(let k=0;k<l/2;k++){const x=i+k,y=x+l/2;const tr=re[y]*cr-im[y]*ci,ti=re[y]*ci+im[y]*cr;re[y]=re[x]-tr;im[y]=im[x]-ti;re[x]+=tr;im[x]+=ti;const q=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=q;}}}}
function centroid(r,d,t){const N=512,s=t*45,re=new Float64Array(N),im=new Float64Array(N);let e=0;
  for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));const v=(r.out[s+k]-d[s+k])/PEAK*h;re[k]=v;im[k]=0;e+=v*v;}
  fft(re,im);let num=0,den=0;for(let b=2;b<N/2;b++){const m=Math.hypot(re[b],im[b]);num+=b/N*FS*m;den+=m;}
  return {c:den>1e-9?num/den/1000:0, rms:Math.sqrt(e/N)/PEAK*100};}
const good=LR('/tmp/good_sim.html'), dart=LR('/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/chiff_sim.html');
const gp={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:96,seed:9,warp:1,floor:0,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4,dutySeed:0};
const dp={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:96,seed:9,chiffDur:600};
const gr=good(gp),grd=good({...gp,amount:0}).out, dr=dart(dp),drd=dart({...dp,amount:0}).out;
console.log(' t(ms) | GOOD: centroid/rms | DART: centroid/rms   (centroid kHz = brightness; want it to FALL)');
for(const t of [25,60,100,150,220,300]){const g=centroid(gr,grd,t),d=centroid(dr,drd,t);
  console.log(`  ${String(t).padStart(4)} |  ${g.c.toFixed(1).padStart(4)}kHz ${g.rms.toFixed(1).padStart(4)}%  |  ${d.c.toFixed(1).padStart(4)}kHz ${d.rms.toFixed(1).padStart(4)}%`);}
