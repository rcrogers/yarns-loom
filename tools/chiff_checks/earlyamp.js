// Early-chiff amplitude vs chiff duration: does 1s vs 8s change the onset?
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);

function measure(durMs, amount){
  const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:9000,
    amount,chiffDur:durMs,seed:0xCAFEBABE};
  const r=mod.render(p);
  // noise = mean |sample-to-sample step| per window (transient content), % of PEAK
  const win=(aMs,bMs)=>{let s=0,n=0;
    for(let i=Math.max(1,aMs*45);i<bMs*45;i++){s+=Math.abs(r.out[i]-r.out[i-1]);n++;}
    return s/n/PEAK*100;};
  return [win(0,50),win(50,100),win(100,200),win(200,400)];
}
const chiffN=d=>Math.round(d*45);
const shiftOf=n=>Math.max(0,Math.log2(Math.max(1,n))-2);
for(const amount of [32,64,96,127]){
  const w=(1-Math.exp(-4*amount/127))/(1-Math.exp(-4));
  console.log(`amount=${amount} (warped ${w.toFixed(3)})`);
  for(const d of [1000,8000]){
    const shD=shiftOf(chiffN(d)), shB=shD-w*(shD-1);
    const m=measure(d,amount).map(x=>x.toFixed(2)).join('  ');
    console.log(`  dur=${String(d).padStart(4)}ms shDark=${shD.toFixed(2)} shBright=${shB.toFixed(2)}  noise% [0-50|50-100|100-200|200-400ms]: ${m}`);
  }
}
