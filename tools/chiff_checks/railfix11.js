// FULL-FIT: maintain full dart depth; shift the dart CENTER down only as far
// as needed to fit under the top rail: center = min(dialed, maxLevel - amp).
// Symmetric 50/50 darts around it. Dip = dialed - center <= amp (inside the
// band -> masked); -> 0 as amp fades or the stage leaves the rail.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const CUR=src.slice(src.indexOf('      const r = rand();'),src.indexOf('      pert +='));
const FULLFIT=`      const r = rand();
      let dartTgt = 0;
      const center = Math.min(dialed, maxLevel - amp);
      if ((r & 0xFFFF) < DTHRESH) {
        dartTgt = (center - dialed) + ((r & 0x10000) ? amp : -amp);
      }
`;
function build(patch,thresh){const mod={};
  eval(src.replace(CUR,patch.replace('DTHRESH',thresh))+'\nmod.render=render;');return mod.render;}
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));
function analyze(render,p,wins,label){
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  const peakLevel=Math.round(PEAK*p.peak);
  let atTop=0,topRun=0,topMax=0,atFloor=0,floorRun=0,floorMax=0;
  for(let i=0;i<r.totalN;i++){
    if(r.out[i]===peakLevel){atTop++;topRun++;topMax=Math.max(topMax,topRun);}else topRun=0;
    if(r.out[i]===0&&i<r.gateN){atFloor++;floorRun++;floorMax=Math.max(floorMax,floorRun);}else floorRun=0;}
  const hi=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;m+=r.out[i]-d.out[i];n++;}
    hi.push(Math.sqrt(s/n)/PEAK*1e3);bias.push(m/n/PEAK*100);}
  let hopMin=1e18,hopSum=0,hopN=0;
  const [ha,hb]=p.attack>=1000?[600,1200]:[100,200];
  for(let h=ha*45;h+64<=hb*45;h+=64){let s=0;
    for(let i=h;i<h+64;i++)s+=Math.abs(r.out[i]-r.out[i-1]);
    s/=64;hopMin=Math.min(hopMin,s);hopSum+=s;hopN++;}
  console.log(`${label}: top ${atTop} (${(topMax/45).toFixed(2)}ms) floor ${atFloor} (${(floorMax/45).toFixed(2)}ms) hop ${(hopMin/(hopSum/hopN)*100).toFixed(0)}%`);
  console.log('  hiRMS '+hi.map(x=>x.toFixed(1).padStart(7)).join(''));
  console.log('  bias  '+bias.map(x=>x.toFixed(1).padStart(7)).join(''));
}
const shortP={attack:200,decay:300,peak:0.7,sustain:0.7,release:400,gate:1000,amount:96,chiffDur:600,seed:0xCAFEBABE};
const shortW=[[0,50],[50,100],[100,150],[150,200],[200,250],[250,300],[300,400],[400,500]];
const longP={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
const longW=[[0,100],[100,300],[300,600],[600,900],[900,1200],[1200,1600],[1600,2400]];
{ // current file as-is (v3.1)
  const mod={};eval(src+'\nmod.render=render;');
  analyze(mod.render,shortP,shortW,'v3.1 (current)   short');
  analyze(mod.render,longP,longW,'v3.1 (current)   8s   ');
}
for(const [t,name] of [['0x8000','FULL-FIT      '],['0x8000 * (1 + Math.max(0, Math.min(1, 1 - (maxLevel-dialed)/amp)))','FULL-FIT+dens ']]){
  const render=build(FULLFIT,t);
  analyze(render,shortP,shortW,name+' short');
  analyze(render,longP,longW,name+' 8s   ');
}
