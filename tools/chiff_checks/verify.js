const fs=require('fs');
const h=fs.readFileSync('/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/chiff_sim.html','utf8');
const c=h.slice(h.indexOf('const FS = 45000'),h.indexOf('function dialed('));const m={};eval(c+'\nm.render=render;');
const FS=45000,PEAK=(1<<30)-(1<<15);
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=l){let cr=1,ci=0;for(let k=0;k<l/2;k++){const x=i+k,y=x+l/2;const tr=re[y]*cr-im[y]*ci,ti=re[y]*ci+im[y]*cr;re[y]=re[x]-tr;im[y]=im[x]-ti;re[x]+=tr;im[x]+=ti;const q=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=q;}}}}
function meas(p,t){const r=m.render(p),d=m.render({...p,amount:0}).out;const N=512,s=t*45,re=new Float64Array(N),im=new Float64Array(N);let e=0;for(let k=0;k<N;k++){const w=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));const v=(r.out[s+k]-d[s+k])/PEAK*w;re[k]=v;e+=v*v;}fft(re,im);let hi=0,tot=0;for(let b=1;b<N/2;b++){const mg=Math.hypot(re[b],im[b]);tot+=mg;if(b/N*FS>3000)hi+=mg;}return{rms:Math.sqrt(e/N)*100,hf:tot>1e-9?hi/tot*100:0,r,d};}
console.log('AMOUNT sweep (onset t=60ms): loudness AND brightness should both rise with amount');
console.log(' amt | noise% | hi-band%(>3kHz)');
for(const a of [1,8,32,64,127]){const x=meas({attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:a,seed:9,chiffDur:600},60);
  console.log(`  ${String(a).padStart(3)} |  ${x.rms.toFixed(2).padStart(5)} |   ${x.hf.toFixed(0)}`);}
console.log('\nRELEASE ACCELERATION: early release (60ms) into 1200ms chiff, short 150ms release.');
console.log('chiff noise should fade to ~0 by release-end, not extend past it:');
const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:150,gate:60,amount:96,seed:9,chiffDur:1200};
const r=m.render(p),d=m.render({...p,amount:0}).out;
for(let t=60;t<=320;t+=30){let s=0,n=0;for(let i=t*45;i<(t+30)*45&&i<r.totalN;i++){if(i>0)s+=Math.abs(r.out[i]-d[i]);n++;}
  const rel=t<60?'':t<210?' [release]':' [after]';console.log(`  ${String(t).padStart(4)}ms: ${(s/n/PEAK*100).toFixed(2)}%${rel}`);}
