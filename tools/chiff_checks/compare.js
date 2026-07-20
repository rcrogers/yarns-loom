const fs=require('fs');
function loadRender(f){ const h=fs.readFileSync(f,'utf8'); const c=h.slice(h.indexOf('const FS = 45000'),h.indexOf('function dialed(')); const m={}; eval(c+'\nm.render=render;'); return m.render; }
const FS=45000,PEAK=(1<<30)-(1<<15);
function fft(re,im){const n=re.length;for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){[re[i],re[j]]=[re[j],re[i]];[im[i],im[j]]=[im[j],im[i]];}}for(let l=2;l<=n;l<<=1){const a=-2*Math.PI/l,wr=Math.cos(a),wi=Math.sin(a);for(let i=0;i<n;i+=l){let cr=1,ci=0;for(let k=0;k<l/2;k++){const x=i+k,y=x+l/2;const tr=re[y]*cr-im[y]*ci,ti=re[y]*ci+im[y]*cr;re[y]=re[x]-tr;im[y]=im[x]-ti;re[x]+=tr;im[x]+=ti;const q=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=q;}}}}
const good=loadRender('/tmp/good_sim.html');
const dart=loadRender('/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/chiff_sim.html');
const common={attack:600,decay:1,peak:0.7,sustain:0.6,release:300,gate:1400,amount:96,seed:0xCAFEBABE};
const goodP={...common,warp:1,floor:0,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4,dutySeed:0};
const dartP={...common,chiffDur:600};
function measure(render,p,tag){
  const r=render(p); const d=render({...p,amount:0}).out;
  // AC noise (rms of out-localmean) per 50ms, + peak; and residual spectrum tonality @150ms
  let peakN=0; const curve=[];
  for(let t=0;t<700;t+=50){let e=0,n=0,a=t*45+16,b=(t+50)*45-16,w=16;
    for(let i=a;i<b&&i<r.totalN;i++){let mn=0;for(let j=-w;j<=w;j++)mn+=r.out[i+j];mn/=(2*w+1);e+=(r.out[i]-mn)**2;n++;}
    const rms=Math.sqrt(e/n)/PEAK*100; curve.push(rms); if(rms>peakN)peakN=rms;}
  const N=1024,s=150*45,re=new Float64Array(N),im=new Float64Array(N);
  for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));re[k]=((r.out[s+k]-d[s+k])/PEAK)*h;im[k]=0;}
  fft(re,im); let pk=0,pf=0,su=0; for(let bb=2;bb<N/2;bb++){const mg=Math.hypot(re[bb],im[bb]);if(mg>pk){pk=mg;pf=bb/N*FS;}su+=mg;}
  console.log(`${tag}: peakNoise=${peakN.toFixed(1)}%  fade(50ms steps): ${curve.map(x=>x.toFixed(1)).join(' ')}`);
  console.log(`    spectrum @150ms: peak ${(pf/1000).toFixed(1)}kHz, peak/mean=${(pk/(su/(N/2-2))).toFixed(1)} (>>3 = tonal buzz)`);
}
measure(good,goodP,'GOOD(duty-binary)');
measure(dart,dartP,'DART(current)   ');
