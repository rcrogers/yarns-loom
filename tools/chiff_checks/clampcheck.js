// Is the early-chiff amplitude loss the [0,maxLevel] clamp trimming down-darts
// while dialed is still low? Compare clamped vs unclamped excursion over time.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
// capture unclamped dialed+pert alongside the real output
code=code.replace('out[i] = Math.max(0, Math.min(maxLevel, dialed + pert));',
  'out[i] = Math.max(0, Math.min(maxLevel, dialed + pert)); global._raw[i] = dialed + pert; global._dialed[i] = dialed;');
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);

function run(durMs, amount, attack){
  const p={attack,decay:1,peak:0.7,sustain:0.6,release:2000,gate:9000,
    seed:0xCAFEBABE,chiffDur:durMs,amount};
  global._raw=new Float64Array(9.5*45000); global._dialed=new Float64Array(9.5*45000);
  const r=mod.render(p);
  const win=(aMs,bMs)=>{let sc=0,sr=0,ncl=0,n=0;
    for(let i=aMs*45;i<bMs*45;i++){
      const ec=r.out[i]-global._dialed[i], er=global._raw[i]-global._dialed[i];
      sc+=ec*ec; sr+=er*er; if(r.out[i]!==global._raw[i])ncl++; n++;}
    return {clamped:Math.sqrt(sc/n)/PEAK*100, raw:Math.sqrt(sr/n)/PEAK*100, pct:ncl/n*100};};
  console.log(`  dur=${String(durMs).padStart(4)}ms attack=${attack}ms`);
  for(const [a,b] of [[0,50],[50,100],[100,200],[200,400],[400,800]]){
    const w=win(a,b);
    console.log(`    ${String(a).padStart(3)}-${String(b).padEnd(3)}ms: clamped ${w.clamped.toFixed(2)}%  unclamped ${w.raw.toFixed(2)}%  samples clamped ${w.pct.toFixed(0)}%`);
  }
}
console.log('amount=96');
run(1000,96,1200);
run(8000,96,1200);
