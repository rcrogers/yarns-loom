// Banding index: std/mean (CV) and worst-column of per-column HIGH-BAND
// energy (2nd-difference RMS, 64-sample columns) near the attack peak,
// across amount x W_MAX. Also the dip (bias) so the tradeoff stays visible.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const baseCode=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const FS=45000, PEAK=(1<<30)-(1<<15);
function build(wmax){
  const code=baseCode.replace('const W_MAX = 3 / 4;',`const W_MAX = ${wmax};`);
  if(code===baseCode && wmax!=='3 / 4'){console.error('anchor missing');process.exit(1);}
  const mod={}; eval(code+'\nmod.render=render;'); return mod.render;
}
function bandIndex(render,amount){
  const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount,chiffDur:601,seed:0xCAFEBABE};
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  // near-peak window 90-250ms; hi-band proxy = 2nd difference RMS per column
  const cols=[];
  for(let h=90*45;h+64<=250*45;h+=64){let s=0;
    for(let i=h+1;i<h+64;i++){const dd=(r.out[i+1]-r.out[i])-(r.out[i]-r.out[i-1]);s+=dd*dd;}
    cols.push(Math.sqrt(s/62));}
  const mean=cols.reduce((a,b)=>a+b)/cols.length;
  const cv=Math.sqrt(cols.reduce((a,b)=>a+(b-mean)*(b-mean),0)/cols.length)/mean;
  const worst=Math.min(...cols)/mean;
  let m=0,n=0;
  for(let i=100*45;i<200*45;i++){m+=r.out[i]-d.out[i];n++;}
  return {cv:cv*100, worst:worst*100, bias:m/n/PEAK*100};
}
console.log('W_MAX      amt32: CV worst bias   |  amt96: CV worst bias');
for(const wmax of ['0.5','0.625','0.75','0.875']){
  const render=build(wmax);
  const a=bandIndex(render,32), b=bandIndex(render,96);
  console.log(wmax.padEnd(9)
    +`  ${a.cv.toFixed(0).padStart(4)}% ${a.worst.toFixed(0).padStart(4)}% ${a.bias.toFixed(1).padStart(5)}%`
    +`   |  ${b.cv.toFixed(0).padStart(4)}% ${b.worst.toFixed(0).padStart(4)}% ${b.bias.toFixed(1).padStart(5)}%`);
}
