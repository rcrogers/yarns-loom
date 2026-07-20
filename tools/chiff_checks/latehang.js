// Late-window early release: gate 7s into an 8s chiff, release 100ms.
// The value must fall with the release, not hang on the near-DC chiff slew.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const p={attack:200,decay:200,peak:0.7,sustain:0.6,release:100,gate:7000,
  amount:96,chiffDur:8000,seed:0xCAFEBABE};
const r=mod.render(p);
for(const tMs of [7000,7050,7100,7150,7200,7300]){
  const i=Math.min(tMs*45, r.totalN-1);
  console.log(`  t=${tMs}ms: ${(r.out[i]/PEAK*100).toFixed(1)}%`);
}
