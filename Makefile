# One entry point for everything built from yarns/envelope.cc.
#
# Most consumers self-freshen: the host battery (tools/hosttest) and the QEMU
# differential (tools/qemutest) recompile envelope.cc on every run, so they
# cannot go stale. The SIM is the exception -- its engine is compiled once and
# inlined into the committed chiff_sim.html -- so it is the only thing `make`
# rebuilds, and it rebuilds unconditionally (see `sim` below for why).
#
#   make            rebuild the sim, then verify (host + qemu + parity)
#   make sim        rebuild + re-inline the sim engine (always)
#   make host       host C reference: UBSan, golden, battery, anomaly
#   make cv         host CV output path: Voice::NoteOn -> CVOutput -> DAC
#   make qemu       differential: render-loop asm == C reference, under QEMU
#   make check      verify the CURRENT tree without rebuilding the sim
#   make firmware   build the flashable .syx (regenerates resources.*)
#   make cycles     what the envelope costs per block, against the baseline

.PHONY: all sim host cv qemu check firmware cycles

# Rebuild the sim, then run the full verification.
all: sim check

# The sim's engine is Emscripten-compiled and inlined into the committed
# chiff_sim.html. That page is ALSO a source (its hand-edited UI), so mtime
# dependency tracking is UNSOUND: editing the page makes it look newer than the
# engine sources and the rebuild gets skipped, silently keeping a stale engine.
# So `sim` always rebuilds (needs Docker / emscripten). `make check` verifies
# the current tree WITHOUT rebuilding, so it still catches staleness as a
# simparity failure.
sim:
	sh tools/simengine/build.sh

host:
	sh tools/hosttest/build.sh

# The CV OUTPUT PATH -- Voice::NoteOn through CVOutput to the DAC -- which the
# envelope harness starts past and the sim stops short of.
cv:
	sh tools/cvtest/build.sh

qemu:
	sh tools/qemutest/verify.sh

# Verify the tree is in sync WITHOUT rebuilding, so a stale committed sim shows
# up as a simparity failure rather than being silently refreshed.
check: host cv qemu
	node tools/chiff_checks/simparity.js chiff_sim.html
	node tools/chiff_checks/peakfloor.js
	node tools/chiff_checks/xvmod.js
# The envelope has no per-block artifact of its own. Run at tremolo 0, which is
# where that is the only thing being asked; blockedge.js says why.
	node tools/hosttest/blockedge.js 0 0
# And the tremolo bias's once-a-block ramp stays where it was measured.
	node tools/hosttest/blockrate.js
	node tools/chiff_checks/strictmode.js
# decay.js is BACK IN THE GATE (2026-08-08). It left when 8e1764dd fixed its
# statistic to see slow content and its old 6 dB / 15 dB limits then rejected
# marked (8 settings) and this build (14) -- the limits, not the builds, were
# what was unproven. The stated condition for readmission was a build whose
# character the user had signed off on; e93e4006 is flash-tested and signed off
# ("perf is adequate, basic chiff quality is good"), so the limits are
# recalibrated to its measured spread and it is green. Read it as a REGRESSION
# GUARD on that character, not as a smoothness oracle -- decay.js says why.
	node tools/chiff_checks/decay.js

firmware:
	SKIP_PROGRAMMING=true ./env/mutable-env.sh make -f yarns/makefile syx

# The render loop runs 12 times per sample, so one instruction there is ~0.7%
# of the whole CPU. Builds first: cycles.sh reads build/yarns/yarns.elf, and a
# STALE elf answers confidently about a build that is not the tree.
cycles: firmware
	sh tools/cycles.sh
