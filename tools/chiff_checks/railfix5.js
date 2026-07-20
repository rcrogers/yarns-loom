// SYM+BRIGHT-COMP on the ORIGINAL dark-lines scenario (8s chiff, held note).
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const CAPPED=`        const upRoom = maxLevel - dialed;
        if (dartTgt > RAIL_CAP * upRoom) dartTgt = RAIL_CAP * upRoom;
`;
if(src.includes('RAIL_CAP * upRoom')) src=src.replace(CAPPED,'');
const ORIG='dartTgt = (r & 0x10000) ? amp : -amp;           // RANDOM sign -> broadband, not a tone';
const SLEW='pert += (dartTgt - pert) * alphaOf(chiffShift);';
function build(dartPatch,slewPatch){const mod={};
  eval(src.replace(ORIG,dartPatch).replace(SLEW,slewPatch||SLEW)+'\nmod.render=render;');
  return mod.render;}
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));
const p={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),
  gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
const peakLevel=Math.round(PEAK*p.peak);
function analyze(render,label){
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  let atRail=0,run=0,longest=0;
  for(let i=0;i<r.totalN;i++){
    if(r.out[i]===peakLevel){atRail++;run++;longest=Math.max(longest,run);}else run=0;}
  const wins=[[0,100],[100,300],[300,600],[600,900],[900,1200],[1200,1600],[1600,2400]];
  const noise=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){s+=Math.abs(r.out[i]-r.out[i-1]);m+=r.out[i]-d.out[i];n++;}
    noise.push(s/n/PEAK*1e3);bias.push(m/n/PEAK*100);}
  let hopMin=1e18,hopSum=0,hopN=0;
  for(let h=600*45;h+64<=1200*45;h+=64){let s=0;
    for(let i=h;i<h+64;i++)s+=Math.abs(r.out[i]-r.out[i-1]);
    s/=64;hopMin=Math.min(hopMin,s);hopSum+=s;hopN++;}
  console.log(`${label}: rail ${atRail} (longest ${(longest/45).toFixed(2)}ms) darkestHop ${(hopMin/(hopSum/hopN)*100).toFixed(0)}% of regional mean`);
  console.log('  noise '+noise.map(x=>x.toFixed(1)).join(' | '));
  console.log('  bias  '+bias.map(x=>x.toFixed(1)).join(' | '));
}
analyze(build(ORIG),'BASELINE        ');
analyze(build(
`const effAmp = Math.min(amp, 2 * (maxLevel - dialed));
        dartTgt = (r & 0x10000) ? effAmp : -effAmp;
        global._boost = Math.log2(amp / Math.max(effAmp, 1));`,
`pert += (dartTgt - pert) * alphaOf(Math.max(0, chiffShift - (global._boost||0)));`),
'SYM+BRIGHT-COMP ');
