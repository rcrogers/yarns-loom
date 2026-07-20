const fs=require('fs'); const {execSync}=require('child_process');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const cur={}; eval(code+'\ncur.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
function held(render,label){
  const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:8000,
    amount:96,chiffDur:8000,seed:0xCAFEBABE};
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  const wins=[[0,100],[300,600],[600,900],[900,1200]];
  const hi=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;m+=r.out[i]-d.out[i];n++;}
    hi.push(Math.sqrt(s/n)/PEAK*1e3);bias.push(m/n/PEAK*100);}
  let hopMin=1e18,hopSum=0,hopN=0;
  for(let h=600*45;h+64<=1200*45;h+=64){let s=0;
    for(let i=h;i<h+64;i++)s+=Math.abs(r.out[i]-r.out[i-1]);
    s/=64;hopMin=Math.min(hopMin,s);hopSum+=s;hopN++;}
  console.log(label+' hop '+(hopMin/(hopSum/hopN)*100).toFixed(0).padStart(3)+'%'
    +'  hiRMS '+hi.map(x=>x.toFixed(0).padStart(5)).join('')
    +'  bias '+bias.map(x=>x.toFixed(1).padStart(6)).join(''));
}
held(cur.render,'CURRENT      ');
for(const wmax of ['0.75','0.875','0.9375','0.96875','1.0']){
  delete require.cache[require.resolve('./weightfix4.js')];
  process.env.W_MAX=wmax; process.argv[2]=process.argv[2];
  const m=require('./weightfix4.js');
  held(m.render,('W_MAX='+wmax).padEnd(13));
}
