# Host-side tests: no hardware needed.
#   make -C test host     unit tests of the firmware's pure modules + the browser upload loop
#   make -C test sim      end-to-end: the real server (test/hostsim) driven over HTTP
#   make -C test all
CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -O2 -I host
OUT      := /tmp/pocketwui-tests
UNITS    := test_paths test_qrtext test_textutil test_zip test_fwimage

host: $(addprefix $(OUT)/,$(UNITS))
	@for t in $(UNITS); do (cd .. && $(OUT)/$$t) || exit 1; done
	node host/test_upload.mjs

$(OUT)/%: host/%.cpp host/arduino_shim.h $(wildcard ../src/*.cpp) $(wildcard ../src/*.h)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -o $@ $<

sim:
	$(MAKE) -C hostsim
	python3 e2e/test_api.py
	node e2e/test_ui.mjs

all: host sim

.PHONY: host sim all
