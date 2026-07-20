// CAREFUL-WEIGHT prototype (W2): two-point mixture with the exact solve.
// A = mu + sigma*sqrt((1-w)/w), B = mu - sigma*sqrt(w/(1-w)) hits mean AND
// amplitude exactly for any w; w chosen per run so A kisses the peak rail in
// reach-scaled space (no dip, no top clamp); infeasible -> rails+weight
// (max-variance telegraph). sigma = amp/sqrt(2) (matches 3-point variance).
// Floor clamp kept (the sim-approved loud-onset trim).
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const RUNBLOCK_OLD=`        const center = Math.min(base, maxLevel - reach); // shifted to fit under the rail
        runUp = center + runAmp;
        runDown = center - runAmp;
        runRelax = base;`;
const RUNBLOCK_NEW=`        const reachF = Math.max(1e-9, reach / Math.max(1e-9, runAmp));
        const sigma = runAmp / Math.SQRT2;
        const slackEff = (maxLevel - base) / reachF;    // target-space room to kiss the rail
        const dnSlackEff = base / reachF;
        if (sigma <= slackEff) {
          runW = 0.5; runUp = base + sigma; runDown = base - sigma;
        } else {
          const rr = slackEff / sigma;
          runW = 1 / (1 + rr * rr);
          runUp = base + slackEff;
          runDown = base - sigma / rr;
          if (runDown < base - dnSlackEff) {            // beyond the max-variance bound
            runUp = base + slackEff; runDown = base - dnSlackEff;
            runW = dnSlackEff / (slackEff + dnSlackEff);
          }
        }`;
const SAMPLE_OLD=`      const r = rand();
      let aim = runRelax;
      if ((r & 0xFFFF) < 0x8000) {                      // ~half the samples dart
        aim = (r & 0x10000) ? runUp : runDown;          // RANDOM sign -> broadband
      }`;
const SAMPLE_NEW=`      const r = rand();
      const aim = ((r & 0xFFFF) / 65536 < runW) ? runUp : runDown;`;
if(!src.includes(RUNBLOCK_OLD) || !src.includes(SAMPLE_OLD)){console.error('PATCH ANCHORS NOT FOUND');process.exit(1);}
src=src.replace(RUNBLOCK_OLD,RUNBLOCK_NEW).replace(SAMPLE_OLD,SAMPLE_NEW)
  .replace('let runAmp = 0, runUp = 0, runDown = 0, runRelax = 0;',
           'let runAmp = 0, runUp = 0, runDown = 0, runW = 0.5;');
const mod={}; eval(src+'\nmod.render=render;');
module.exports={render:mod.render};
if(require.main===module){
  const FS=45000, PEAK=(1<<30)-(1<<15);
  console.log('W2 module self-test: render ok, len',
    mod.render({attack:130,decay:256,peak:0.75,sustain:0.55,release:401,
      gate:575,amount:96,chiffDur:601,seed:0xCAFEBABE}).out.length);
}
