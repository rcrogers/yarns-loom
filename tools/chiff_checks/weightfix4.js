// W2-lambda: continuous dip/sparsity family. w capped at W_MAX (dart-rate
// floor); the kiss constraint then sets the center DOWN by whatever the
// capped tilt can't absorb: A = center + sigma*sqrt((1-w)/w) = top_eff.
// W_MAX = 1 -> pure W2 (no dip, sparse); W_MAX = 0.5 -> pure center shift.
const fs=require('fs');
const W_MAX = parseFloat(process.env.W_MAX || '0.9375');  // 15/16
const html=fs.readFileSync(process.argv[2] || process.argv[1].replace(/[^/]*$/,'') + '../../chiff_sim.html','utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const RUNBLOCK_OLD=`        const center = Math.min(base, maxLevel - reach); // shifted to fit under the rail
        runUp = center + runAmp;
        runDown = center - runAmp;
        runRelax = base;`;
const RUNBLOCK_NEW=`        const reachF = Math.max(1e-9, reach / Math.max(1e-9, runAmp));
        const sigma = runAmp / Math.SQRT2;
        const topEff = base + (maxLevel - base) / reachF;
        const floorEff = base - base / reachF;
        const upSpanFree = sigma;                        // w = 1/2 up excursion
        if (base + upSpanFree <= topEff) {
          runW = 0.5; runUp = base + sigma; runDown = base - sigma;
        } else {
          // Tilt up to W_MAX; kiss via center drop for the remainder.
          const slack = topEff - base;
          const wKiss = 1 / (1 + (slack / sigma) * (slack / sigma));
          runW = Math.min(wKiss, ${'W_MAX'});
          const upSpan = sigma * Math.sqrt((1 - runW) / runW);
          const center = topEff - upSpan;                // dip = base - center (0 if wKiss <= W_MAX)
          runUp = center + upSpan;
          runDown = center - sigma * Math.sqrt(runW / (1 - runW));
          if (runDown < floorEff) {                      // max-variance bound
            runUp = topEff; runDown = floorEff;
            runW = (base - floorEff) / (topEff - floorEff);
          }
        }`.replace('${'+"'W_MAX'"+'}', String(W_MAX));
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
