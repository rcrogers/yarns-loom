// W-family, calibration-corrected. Real-space center carries the mean;
// the kiss constraint applies through the reach factor; floor side is left
// to the clamp (approved onset trim), no floor fit.
//   wKiss: tilt at which A kisses with zero dip.
//   w = min(wKiss, W_MAX); center = min(base, top - reachF*upSpan(w)).
// W_MAX=0.5 -> pure symmetric+dip (FULL-FIT-like); 1.0 -> pure W2 (no dip).
const fs=require('fs');
const W_MAX = parseFloat(process.env.W_MAX || '0.9375');
const html=fs.readFileSync(process.env.SIM || 'chiff_sim.html','utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const RUNBLOCK_OLD=`        const center = Math.min(base, maxLevel - reach); // shifted to fit under the rail
        runUp = center + runAmp;
        runDown = center - runAmp;
        runRelax = base;`;
const RUNBLOCK_NEW=`        const reachF = Math.max(1e-9, reach / Math.max(1e-9, runAmp));
        const sigma = runAmp / Math.SQRT2;
        const slackT = (maxLevel - base) / reachF;       // zero-dip up-span allowance
        let w = 0.5;
        if (sigma > slackT) {
          const rr = slackT / sigma;
          w = Math.min(1 / (1 + rr * rr), W_MAX);
        }
        const upSpan = sigma * Math.sqrt((1 - w) / w);
        const center = Math.min(base, maxLevel - reachF * upSpan);
        runUp = center + upSpan;
        runDown = center - sigma * Math.sqrt(w / (1 - w));
        runW = w;`.replace('W_MAX', String(W_MAX));
const SAMPLE_OLD=`      const r = rand();
      let aim = runRelax;
      if ((r & 0xFFFF) < 0x8000) {                      // ~half the samples dart
        aim = (r & 0x10000) ? runUp : runDown;          // RANDOM sign -> broadband
      }`;
const SAMPLE_NEW=`      const r = rand();
      const aim = ((r & 0xFFFF) / 65536 < runW) ? runUp : runDown;`;
if(!src.includes(RUNBLOCK_OLD) || !src.includes(SAMPLE_OLD)){console.error('ANCHORS NOT FOUND');process.exit(1);}
src=src.replace(RUNBLOCK_OLD,RUNBLOCK_NEW).replace(SAMPLE_OLD,SAMPLE_NEW)
  .replace('let runAmp = 0, runUp = 0, runDown = 0, runRelax = 0;',
           'let runAmp = 0, runUp = 0, runDown = 0, runW = 0.5;');
const mod={}; eval(src+'\nmod.render=render;');
module.exports={render:mod.render};
