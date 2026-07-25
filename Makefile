# One entry point for everything built from yarns/envelope.cc.
#
# Most consumers self-freshen: the host battery (tools/hosttest) and the QEMU
# differential (tools/qemutest) recompile envelope.cc on every run, so they
# cannot go stale. The SIM is the exception -- its engine is compiled once and
# inlined into the committed chiff_sim.html -- so it gets real dependency
# tracking here and is the only thing `make` actually rebuilds.
#
#   make            rebuild the sim if stale, then verify (host + qemu + parity)
#   make sim        rebuild + re-inline the sim engine (if envelope.cc changed)
#   make host       host C-reference battery
#   make qemu       differential: render-loop asm == C reference, under QEMU
#   make check      verify the CURRENT tree without rebuilding the sim
#   make firmware   build the flashable .syx (regenerates resources.*)

ENVELOPE_SRC := yarns/envelope.cc yarns/envelope.h yarns/resources.cc
SIM_SRC := $(ENVELOPE_SRC) tools/simengine/engine.cc \
           tools/portable_envelope.py tools/simengine/inline_engine.py

.PHONY: all sim host qemu check firmware

# Rebuild the sim if stale, then run the full verification.
all: sim check

# The sim's engine is inlined into the committed page, so it can drift. Rebuild
# whenever the firmware or the engine wrapper changes. (Needs Docker/emscripten.)
sim: chiff_sim.html
chiff_sim.html: $(SIM_SRC)
	sh tools/simengine/build.sh

host:
	sh tools/hosttest/build.sh

qemu:
	sh tools/qemutest/verify.sh

# Verify the tree is in sync WITHOUT rebuilding, so a stale committed sim shows
# up as a simparity failure rather than being silently refreshed.
check: host qemu
	node tools/chiff_checks/simparity.js chiff_sim.html

firmware:
	SKIP_PROGRAMMING=true ./env/mutable-env.sh make -f yarns/makefile syx
