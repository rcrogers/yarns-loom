// Corrected sweep + dark-column FRACTION metric + latehang + kink checks.
const fs=require('fs');
process.env.SIM=process.argv[2];
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const cur={}; eval(code+'\ncur.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
function evalModel(render,label){
  // 8s held: bias/loudness/darkness in the late attack
  const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:8000,
    amount:96,chiffDur:8000,seed:0xCAFEBABE};
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  const wins=[[0,100],[300,600],[600,900],[900,1200]];
  const hi=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;m+=r.out[i]-d.out[i];n++;}
    hi.push(Math.sqrt(s/n)/PEAK*1e3);bias.push(m/n/PEAK*100);}
  // dark-column fraction: 64-sample hops in 600-1200ms below 20% of region mean
  let hops=[],sum=0;
  for(let h=600*45;h+64<=1200*45;h+=64){let s=0;
    for(let i=h;i<h+64;i++)s+=Math.abs(r.out[i]-r.out[i-1]);
    hops.push(s/64);sum+=s/64;}
  const mean=sum/hops.length;
  const darkFrac=hops.filter(x=>x<0.2*mean).length/hops.length*100;
  // latehang: release 100ms at 7s of 8s window
  const p2={attack:200,decay:200,peak:0.7,sustain:0.6,release:100,gate:7000,
    amount:96,chiffDur:8000,seed:0xCAFEBABE};
  const r2=render(p2);
  const relEnd=r2.out[Math.min(7100*45,r2.totalN-1)]/PEAK*100;
  // kink: window ends in release (defaults-ish)
  const p3={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount:96,chiffDur:800,seed:0xCAFEBABE};
  const r3=render(p3), d3=render(Object.assign({},p3,{amount:0}));
  let worst=0;
  for(let w0=800;w0<950;w0+=10){let m=0,n=0;
    for(let i=w0*45;i<Math.min((w0+10)*45,r3.totalN);i++){m+=r3.out[i]-d3.out[i];n++;}
    if(n)worst=Math.max(worst,Math.abs(m/n));}
  console.log(label+' dark '+darkFrac.toFixed(0).padStart(3)+'%  relEnd '+relEnd.toFixed(1).padStart(5)+'%  kink '+(worst/PEAK*100).toFixed(2)+'%');
  console.log('  hiRMS '+hi.map(x=>x.toFixed(0).padStart(5)).join('')+'  bias '+bias.map(x=>x.toFixed(1).padStart(6)).join(''));
}
evalModel(cur.render,'CURRENT      ');
for(const wmax of ['0.5','0.75','0.875','0.9375','0.96875','1.0']){
  delete require.cache[require.resolve('./weightfix5.js')];
  process.env.W_MAX=wmax;
  evalModel(require('./weightfix5.js').render,('W_MAX='+wmax).padEnd(13));
}
