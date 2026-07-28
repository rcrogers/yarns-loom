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
#   make host       host C-reference battery
#   make qemu       differential: render-loop asm == C reference, under QEMU
#   make check      verify the CURRENT tree without rebuilding the sim
#   make firmware   build the flashable .syx (regenerates resources.*)

.PHONY: all sim host qemu check firmware

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

qemu:
	sh tools/qemutest/verify.sh

# Verify the tree is in sync WITHOUT rebuilding, so a stale committed sim shows
# up as a simparity failure rather than being silently refreshed.
check: host qemu
	node tools/chiff_checks/simparity.js chiff_sim.html
	node tools/chiff_checks/peakfloor.js
	node tools/chiff_checks/strictmode.js

firmware:
	SKIP_PROGRAMMING=true ./env/mutable-env.sh make -f yarns/makefile syx
