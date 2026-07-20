const fs=require('fs');
function LR(f){const h=fs.readFileSync(f,'utf8');const c=h.slice(h.indexOf('const FS = 45000'),h.indexOf('function dialed('));const m={};eval(c+'\nm.render=render;');return m.render;}
const FS=45000,PEAK=(1<<30)-(1<<15);
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=l){let cr=1,ci=0;for(let k=0;k<l/2;k++){const x=i+k,y=x+l/2;const tr=re[y]*cr-im[y]*ci,ti=re[y]*ci+im[y]*cr;re[y]=re[x]-tr;im[y]=im[x]-ti;re[x]+=tr;im[x]+=ti;const q=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=q;}}}}
function band(r,d,t){const N=512,s=t*45,re=new Float64Array(N),im=new Float64Array(N);for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));re[k]=(r.out[s+k]-d[s+k])/PEAK*h;}fft(re,im);let hi=0,tot=0;for(let b=1;b<N/2;b++){const m=Math.hypot(re[b],im[b]),f=b/N*FS;tot+=m;if(f>2000)hi+=m;}return{hf:tot>1e-9?hi/tot*100:0,e:tot};}
const good=LR('/tmp/good_sim.html'),dart=LR('/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/chiff_sim.html');
const gp={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:96,seed:9,warp:1,floor:0,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4,dutySeed:0};
const shDark=Math.max(0,Math.log2(600*45)-2);
const dp={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:96,seed:9,chiffDur:600,_shDark:shDark};
const gr=good(gp),grd=good({...gp,amount:0}).out,dr=dart(dp),drd=dart({...dp,amount:0}).out;
console.log(`shDark=${shDark.toFixed(1)}. hf%=energy>2kHz (LPF close => FALLS), bias=mean(out-dialed):`);
console.log(' t(ms) | GOOD hf%/e | DART hf%/e | DART bias%');
for(const t of [25,80,150,250,400,550]){const g=band(gr,grd,t),dd=band(dr,drd,t);
  let bs=0,n=0;for(let i=t*45;i<t*45+900&&i<dr.totalN;i++){bs+=dr.out[i]-drd[i];n++;}
  console.log(`  ${String(t).padStart(4)} |  ${g.hf.toFixed(0).padStart(3)}% ${(g.e*100).toFixed(1).padStart(4)} |  ${dd.hf.toFixed(0).padStart(3)}% ${(dd.e*100).toFixed(1).padStart(4)} | ${(bs/n/PEAK*100).toFixed(2)}`);}
