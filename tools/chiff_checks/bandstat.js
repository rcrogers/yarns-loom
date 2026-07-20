// Multi-seed banding comparison: dark-band count per render (columns whose
// hi-band energy < 25% of the render's column median) + worst-column depth,
// near-peak window, averaged over seeds. Args: <simA.html> <simB.html>
const fs=require('fs');
const FS=45000, PEAK=(1<<30)-(1<<15);
function load(path){
  const html=fs.readFileSync(path,'utf8');
  const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
  const mod={}; eval(code+'\nmod.render=render;'); return mod.render;
}
function darkBands(render,amount,seed){
  const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount,chiffDur:601,seed};
  const r=render(p);
  const cols=[];
  for(let h=90*45;h+64<=250*45;h+=64){let s=0;
    for(let i=h+1;i<h+64;i++){const dd=(r.out[i+1]-r.out[i])-(r.out[i]-r.out[i-1]);s+=dd*dd;}
    cols.push(Math.sqrt(s/62));}
  const sorted=cols.slice().sort((a,b)=>a-b);
  const median=sorted[Math.floor(sorted.length/2)];
  return {dark:cols.filter(x=>x<0.25*median).length,
          worst:sorted[0]/median*100};
}
const seeds=[]; let s=0xCAFEBABE;
for(let i=0;i<20;i++){ s^=(s<<13); s>>>=0; s^=(s>>>17); s^=(s<<5); s>>>=0; seeds.push(s); }
for(const [path,label] of [[process.argv[2],'A'],[process.argv[3],'B']]){
  const render=load(path);
  for(const amount of [32,96]){
    let dark=0,worst=0;
    for(const seed of seeds){const x=darkBands(render,amount,seed);dark+=x.dark;worst+=x.worst;}
    console.log(`${label} amt=${amount}: avg dark bands/render ${(dark/seeds.length).toFixed(1)}, avg worst col ${(worst/seeds.length).toFixed(0)}% of median`);
  }
}
