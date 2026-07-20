// Run the sim FILE as-is on both scenarios; numbers should match the
// SYM+BRIGHT-COMP prototype rows from railfix4/5.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));
function analyze(p,wins,label){
  const r=mod.render(p), d=mod.render(Object.assign({},p,{amount:0}));
  const peakLevel=Math.round(PEAK*p.peak);
  let atRail=0,run=0,longest=0;
  for(let i=0;i<r.totalN;i++){
    if(r.out[i]===peakLevel){atRail++;run++;longest=Math.max(longest,run);}else run=0;}
  const hi=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;m+=r.out[i]-d.out[i];n++;}
    hi.push(Math.sqrt(s/n)/PEAK*1e3);bias.push(m/n/PEAK*100);}
  console.log(`${label}: rail ${atRail} (longest ${(longest/45).toFixed(2)}ms)`);
  console.log('  hiRMS '+hi.map(x=>x.toFixed(1).padStart(7)).join(''));
  console.log('  bias  '+bias.map(x=>x.toFixed(1).padStart(7)).join(''));
}
analyze({attack:200,decay:300,peak:0.7,sustain:0.7,release:400,gate:1000,
  amount:96,chiffDur:600,seed:0xCAFEBABE},
  [[0,50],[50,100],[100,150],[150,200],[200,250],[250,300],[300,400],[400,500]],
  'short-attack (user scenario)');
analyze({attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),
  gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE},
  [[0,100],[100,300],[300,600],[600,900],[900,1200],[1200,1600],[1600,2400]],
  '8s-chiff held (dark-lines scenario)');
