const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000,PEAK=(1<<30)-(1<<15);
function chk(P,tag){
  const r=mod.render(P); const d=mod.render({...P,amount:0}).out;
  let blow=false, maxbias=0, maxout=0;
  for(let i=0;i<r.totalN;i++){ if(!isFinite(r.out[i])||r.out[i]<-0.01*PEAK||r.out[i]>PEAK*1.01){blow=true;} if(Math.abs(r.out[i])>maxout)maxout=Math.abs(r.out[i]); }
  // DC bias per 40ms window, max
  for(let t=0;t<r.totalN;t+=40*45){let s=0,n=0;for(let i=t;i<t+40*45&&i<r.totalN;i++){s+=r.out[i]-d[i];n++;} if(Math.abs(s/n)>Math.abs(maxbias))maxbias=s/n;}
  // end-of-burst cutoff: noise 20ms before chiff end vs at end
  const chN=Math.round(P.chiffDur*45); const pre=(a)=>{let s=0,n=0;for(let i=a;i<a+900&&i<r.totalN;i++){if(i>0)s+=Math.abs(r.out[i]-d[i]);n++;}return s/n/PEAK*100;};
  console.log(`${tag.padEnd(34)} maxbias=${(maxbias/PEAK*100).toFixed(2).padStart(6)}% peakout=${(maxout/PEAK*100).toFixed(0).padStart(3)}% noise@[end-20ms]=${pre(chN-900).toFixed(2)}% @[end+10ms]=${pre(chN+450).toFixed(2)}% ${blow?'*** BLOWUP':'ok'}`);
}
const B={attack:200,decay:100,peak:0.7,sustain:0.6,release:300,gate:1500,amount:96,chiffDur:600,seed:0xCAFEBABE};
chk({...B},'baseline');
chk({...B,gate:60},'early release (mid-attack)');
chk({...B,chiffDur:150},'chiff shorter than attack');
chk({...B,chiffDur:2000},'chiff longer than note');
chk({...B,amount:127},'amount max');
chk({...B,amount:127,peak:1.0,sustain:1.0},'amount max, peak&sustain full');
chk({...B,sustain:0.0},'sustain 0');
chk({...B,gate:60,release:60,chiffDur:1200},'early+short release, long chiff');
