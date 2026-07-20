// Why does the noise hang BELOW the level in sustain? Capture pert directly.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
code=code.replace('pert += (dartTgt - pert) * alphaOf(chiffShift);',
  'pert += (dartTgt - pert) * alphaOf(chiffShift); if(global._cap){global._cap.pert.push(pert);global._cap.tgt.push(dartTgt);}');
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));

const p={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),
  gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
global._cap={pert:[],tgt:[]};
mod.render(p);
const {pert,tgt}=global._cap;
function stats(a,b,label){
  let s=0,s2=0,mn=1e18,mx=-1e18,n=0,up=0,dn=0,z=0;
  for(let i=a;i<b;i++){const v=pert[i];s+=v;s2+=v*v;if(v<mn)mn=v;if(v>mx)mx=v;n++;
    if(tgt[i]>0)up++;else if(tgt[i]<0)dn++;else z++;}
  const f=x=>(x/PEAK*100).toFixed(1);
  console.log(`${label}: mean ${f(s/n)}% rms ${f(Math.sqrt(s2/n))}% min ${f(mn)}% max ${f(mx)}%  darts up ${up} dn ${dn} zero ${z}`);
}
stats(45*100,45*200,'attack 100-200ms  ');
stats(45*1600,45*1700,'sustain 1.6-1.7s ');
stats(45*4000,45*4100,'sustain 4.0-4.1s ');
stats(45*7000,45*7100,'sustain 7.0-7.1s ');
