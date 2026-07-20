const fs=require('fs');
function LR(f){const h=fs.readFileSync(f,'utf8');const c=h.slice(h.indexOf('const FS = 45000'),h.indexOf('function dialed('));const m={};eval(c+'\nm.render=render;');return m.render;}
const FS=45000,PEAK=(1<<30)-(1<<15);
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=l){let cr=1,ci=0;for(let k=0;k<l/2;k++){const x=i+k,y=x+l/2;const tr=re[y]*cr-im[y]*ci,ti=re[y]*ci+im[y]*cr;re[y]=re[x]-tr;im[y]=im[x]-ti;re[x]+=tr;im[x]+=ti;const q=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=q;}}}}
const dart=LR('/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/chiff_sim.html');
const base={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:96,chiffDur:600,seed:0xCAFEBABE};
function m(p){const r=dart(p),d=dart({...p,amount:0}).out; let pn=0;for(let t=0;t<300;t+=50){let e=0,n=0,a=t*45+16,b=(t+50)*45-16,w=16;for(let i=a;i<b&&i<r.totalN;i++){let mn=0;for(let j=-w;j<=w;j++)mn+=r.out[i+j];mn/=(2*w+1);e+=(r.out[i]-mn)**2;n++;}const rms=Math.sqrt(e/n)/PEAK*100;if(rms>pn)pn=rms;}
 const N=1024,s=150*45,re=new Float64Array(N),im=new Float64Array(N);for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));re[k]=((r.out[s+k]-d[s+k])/PEAK)*h;} fft(re,im);let pk=0,pf=0;for(let bb=2;bb<N/2;bb++){const mg=Math.hypot(re[bb],im[bb]);if(mg>pk){pk=mg;pf=bb/N*FS;}} return {pn,pf};}
console.log('TARGET (good): peakNoise 12.6%, spectrum ~1.1kHz');
for(const sh of [0.5,1,1.5,2]) for(const kk of [0.9,1.5,2.5]){const {pn,pf}=m({...base,_sh:sh,_k:kk});console.log(`  shift=${sh} k=${kk}: peakNoise=${pn.toFixed(1)}%  spec=${(pf/1000).toFixed(1)}kHz`);}
