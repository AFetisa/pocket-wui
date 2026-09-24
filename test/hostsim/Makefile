# Host simulator: builds the firmware's server sources for the PC.
#   make run    -> http://127.0.0.1:8791/ with password test1234, card in /tmp/pocketwui-sd
SRC      := ../../src
FIRMWARE := auth paths textutil zipstream fwimage http_util api_files api_device dav stream jobs storage server
OBJS     := $(addprefix build/,$(addsuffix .o,$(FIRMWARE))) build/sim_httpd.o build/sim_sd.o build/sim_stubs.o build/sim_main.o
CXXFLAGS ?= -O1 -g
CXXFLAGS += -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format \
            -Iinclude -I../host -Ibuild -DWUI_HTTP_PORT=wui_sim_port -include include/sim_prelude.h
LDFLAGS  += -pthread

pocketwui-sim: $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

build/web_assets.h: ../../web/index.html ../../tools/embed_web.py
	@mkdir -p build
	cd ../.. && python3 tools/embed_web.py
	cp $(SRC)/web_assets.h $@

build/%.o: $(SRC)/%.cpp build/web_assets.h $(wildcard $(SRC)/*.h) $(wildcard include/*.h)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: %.cpp $(wildcard include/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: pocketwui-sim
	./pocketwui-sim --root /tmp/pocketwui-sd

clean:
	rm -rf build pocketwui-sim

.PHONY: run clean
