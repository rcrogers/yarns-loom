// SAG-BUDGET generalization of balanced-aim, top rail only.
//   S   = SAG_FRACTION * amp           (dip budget, fades with the burst)
//   sag = min(S, max(0, amp - upRoom)) (engages only near the rail)
//   u   = min(amp, upRoom + sag)       (up-aim kisses the rail from the sagged center)
//   pUp = (amp - sag) / (u + amp)      (aim average = -sag exactly)
// SAG_FRACTION=0 -> zero-dip balanced aim; larger -> louder near rail, more dip.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const CAPPED=`        const upRoom = maxLevel - dialed;
        if (dartTgt > RAIL_CAP * upRoom) dartTgt = RAIL_CAP * upRoom;
`;
if(src.includes('RAIL_CAP * upRoom')) src=src.replace(CAPPED,'');
const ORIG='dartTgt = (r & 0x10000) ? amp : -amp;           // RANDOM sign -> broadband, not a tone';
const mk=f=>`const upRoom = maxLevel - dialed;
        const sag = Math.min(${f} * amp, Math.max(0, amp - upRoom));
        const u = Math.min(amp, upRoom + sag);
        const pUp = (amp - sag) / (u + amp);
        dartTgt = (((r >>> 17) & 0x7FFF) / 32768 < pUp) ? u : -amp;`;
function build(patch){const mod={};eval(src.replace(ORIG,patch)+'\nmod.render=render;');return mod.render;}
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));
function analyze(render,p,wins,label){
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  const peakLevel=Math.round(PEAK*p.peak);
  let atRail=0,run=0,longest=0;
  for(let i=0;i<r.totalN;i++){
    if(r.out[i]===peakLevel){atRail++;run++;longest=Math.max(longest,run);}else run=0;}
  const hi=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;m+=r.out[i]-d.out[i];n++;}
    hi.push(Math.sqrt(s/n)/PEAK*1e3);bias.push(m/n/PEAK*100);}
  let hopMin=1e18,hopSum=0,hopN=0;
  const [ha,hb]=p.attack>=1000?[600,1200]:[100,200];
  for(let h=ha*45;h+64<=hb*45;h+=64){let s=0;
    for(let i=h;i<h+64;i++)s+=Math.abs(r.out[i]-r.out[i-1]);
    s/=64;hopMin=Math.min(hopMin,s);hopSum+=s;hopN++;}
  console.log(`${label}: rail ${atRail} (${(longest/45).toFixed(2)}ms) hop ${(hopMin/(hopSum/hopN)*100).toFixed(0)}%`);
  console.log('  hiRMS '+hi.map(x=>x.toFixed(1).padStart(7)).join(''));
  console.log('  bias  '+bias.map(x=>x.toFixed(1).padStart(7)).join(''));
}
const shortP={attack:200,decay:300,peak:0.7,sustain:0.7,release:400,gate:1000,amount:96,chiffDur:600,seed:0xCAFEBABE};
const shortW=[[0,50],[50,100],[100,150],[150,200],[200,250],[250,300],[300,400],[400,500]];
const longP={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
const longW=[[0,100],[100,300],[300,600],[600,900],[900,1200],[1200,1600],[1600,2400]];
for(const f of [0, 0.125, 0.25]){
  const render=build(mk(f));
  analyze(render,shortP,shortW,`SAG=${f} short `);
  analyze(render,longP,longW,`SAG=${f} 8s    `);
}
