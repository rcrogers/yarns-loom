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
#   make ui         host display: the real driver, its GPIO decoded
#   make osc        host oscillator: all 42 shapes, sample for sample
#   make mix        the summed mix stays inside the span the voices were given
#   make level      what one voice puts out, per shape -- a measurement, no verdict
#   make step       a sample step against the bandwidth that could produce it
#   make warp       every shape's timbre map is monotone, including below zero
#   make qemu       differentials: envelope and oscillator asm == C, under QEMU
#   make check      verify the CURRENT tree without rebuilding the sim
#   make firmware   build the flashable .syx (regenerates resources.*)
#   make cycles     what the envelope costs per block, against the baseline

.PHONY: all sim host cv ui osc warp mix level step qemu check firmware cycles

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

# The DISPLAY, read off the pins it bit-bangs.
ui:
	sh tools/uitest/build.sh

# Every OSCILLATOR SHAPE, sample for sample.
osc:
	sh tools/osctest/build.sh

# THE WARP on its own: monotone across the whole signed timbre range. It is
# where the parameter faults have been, so it gets its own check.
warp:
	sh tools/warptest/run.sh

# THE MIX, against the span voice.h hands out. The contract every shape has to
# keep and none of them declared: driver.cc says why it belongs at the DAC.
mix:
	sh tools/mixtest/build.sh

# WHAT ONE VOICE PUTS OUT. No verdict: these numbers are the input to voicing
# decisions, and a gate on them would freeze a decision nobody has taken.
level:
	sh tools/leveltest/build.sh
	./tools/leveltest/leveltest table

# A SAMPLE STEP against the bandwidth that could have produced it. Also no
# verdict -- the bound holds for the narrowband shapes only, and driver.cc
# carries what it has already ruled out.
#
# In `check` for the COMPILE, which is the rot guard both harnesses need: this
# one builds leveltest first for its generated shape names, so a rename in
# oscillator.h or voice.h breaks the gate instead of a tool nobody ran. The
# scratch version of leveltest died of exactly that, unnoticed.
step:
	sh tools/steptest/build.sh

# THE ASM DIFFERENTIALS. Both prove a hand-written asm path renders exactly what
# the C it replaces does -- the one class of bug a golden cannot see, because a
# golden pins what the code does, not that two implementations agree.
#
# ~20 s each, nearly all of it Docker start-up and the cross-compile. That is
# the price of running the real target, and it is why these are separate from
# the host checks, which are milliseconds.
qemu:
	sh tools/qemutest/verify.sh
	sh tools/oscqemu/verify.sh

# Verify the tree is in sync WITHOUT rebuilding, so a stale committed sim shows
# up as a simparity failure rather than being silently refreshed.
check: host cv ui osc warp mix step qemu
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
