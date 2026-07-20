// W3: relax point kept (half the samples aim at base -> inter-dart motion),
// exact weight solve applied to the DART PAIR (sigma' = sigma*sqrt(2) since
// darts carry the variance on half the samples). Mean exact, amplitude
// exact, rail kissed, and the relax traffic fills the sparse gaps.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const RUNBLOCK_OLD=`        const center = Math.min(base, maxLevel - reach); // shifted to fit under the rail
        runUp = center + runAmp;
        runDown = center - runAmp;
        runRelax = base;`;
const RUNBLOCK_NEW=`        const reachF = Math.max(1e-9, reach / Math.max(1e-9, runAmp));
        const sigma = runAmp;                            // dart-pair sigma (variance on half the samples)
        const slackEff = (maxLevel - base) / reachF;
        const dnSlackEff = base / reachF;
        if (sigma <= slackEff) {
          runW = 0.5; runUp = base + sigma; runDown = base - sigma;
        } else {
          const rr = slackEff / sigma;
          runW = 1 / (1 + rr * rr);
          runUp = base + slackEff;
          runDown = base - sigma / rr;
          if (runDown < base - dnSlackEff) {
            runUp = base + slackEff; runDown = base - dnSlackEff;
            runW = dnSlackEff / (slackEff + dnSlackEff);
          }
        }
        runRelax = base;`;
const SAMPLE_OLD=`      const r = rand();
      let aim = runRelax;
      if ((r & 0xFFFF) < 0x8000) {                      // ~half the samples dart
        aim = (r & 0x10000) ? runUp : runDown;          // RANDOM sign -> broadband
      }`;
const SAMPLE_NEW=`      const r = rand();
      let aim = runRelax;
      if ((r & 0xFFFF) < 0x8000) {                      // ~half the samples dart
        aim = (((r >>> 16) & 0xFFFF) / 65536 < runW) ? runUp : runDown;
      }`;
if(!src.includes(RUNBLOCK_OLD) || !src.includes(SAMPLE_OLD)){console.error('ANCHORS NOT FOUND');process.exit(1);}
src=src.replace(RUNBLOCK_OLD,RUNBLOCK_NEW).replace(SAMPLE_OLD,SAMPLE_NEW)
  .replace('let runAmp = 0, runUp = 0, runDown = 0, runRelax = 0;',
           'let runAmp = 0, runUp = 0, runDown = 0, runRelax = 0, runW = 0.5;');
const mod={}; eval(src+'\nmod.render=render;');
module.exports={render:mod.render};
