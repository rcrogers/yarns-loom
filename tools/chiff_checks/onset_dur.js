// Early-harmonics vs duration: onset spectrum must NOT depend on chiff duration.
// Metric: hiRMS (first-difference RMS, high-band proxy) in 0-50 and 50-100ms,
// across durations, at several amounts. Also amount ramp at default duration.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
function win(r,a,b){let s=0,n=0;
  for(let i=Math.max(1,a*45);i<b*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;n++;}
  return Math.sqrt(s/n)/PEAK*1e3;}
for(const amount of [32,64,96,127]){
  const row=[];
  for(const dur of [300,1000,3000,8000]){
    const r=mod.render({attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,
      gate:9000,amount,chiffDur:dur,seed:0xCAFEBABE});
    row.push(`${dur}ms: ${win(r,0,50).toFixed(1)}/${win(r,50,100).toFixed(1)}`);
  }
  console.log(`amt=${String(amount).padStart(3)}  `+row.join('   '));
}
console.log('amount ramp @ default 601ms duration (peak residual, ~dB vs 127):');
let ref=null;
for(const amount of [1,4,16,64,127]){
  const r=mod.render({attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,
    gate:9000,amount,chiffDur:601,seed:0xCAFEBABE});
  const d=mod.render({attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,
    gate:9000,amount:0,chiffDur:601,seed:0xCAFEBABE});
  let pk=0;for(let i=0;i<r.totalN;i++)pk=Math.max(pk,Math.abs(r.out[i]-d.out[i]));
  if(amount===127)ref=pk;
  console.log(`  amt=${String(amount).padStart(3)}: peak ${(pk/PEAK*100).toFixed(1)}%`);
}
