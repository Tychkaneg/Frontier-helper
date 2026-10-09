CC ?= cc
ZIG ?= zig
WARN := -Wall -Wextra -Werror -Wno-unused-function
.PHONY: test build fixtures
test:
	mkdir -p build
	$(CC) -std=c11 -O2 -UNDEBUG $(WARN) tests/test_core.c -lm -o build/test-core
	build/test-core
build:
	mkdir -p build
	$(ZIG) cc -target x86-windows-gnu -shared -O2 $(WARN) -Wno-unused-parameter -Wl,--image-base,0x60000000 src/native_module.c src/control_win.c -luser32 -ladvapi32 -o build/FrontierOrbitControl.dll
fixtures:
	mkdir -p build
	$(ZIG) cc -target x86-windows-gnu -O2 -UNDEBUG $(WARN) tests/test_pipe.c src/control_win.c -ladvapi32 -o build/test-pipe.exe
	python3 tests/make_fixture_guards.py
	$(ZIG) cc -target x86-windows-gnu -shared -O2 $(WARN) -Wl,--image-base,0x10000000 tests/fake_engine.c -o build/mhfo-hd.dll
	$(ZIG) cc -target x86-windows-gnu -shared -O2 $(WARN) -Wl,--image-base,0x50000000 tests/fake_d3dx.c tests/fake_d3dx.def -o build/d3dx9_43.dll
	$(ZIG) cc -target x86-windows-gnu -O2 -UNDEBUG $(WARN) tests/test_native.c -luser32 -o build/test-native.exe
