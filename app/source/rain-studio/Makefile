ZIG ?= zig
WARN := -Wall -Wextra -Werror -Wno-unused-function
.PHONY: build fixtures test
test:
	mkdir -p build
	cc -std=c11 -O2 -UNDEBUG $(WARN) tests/test_profile.c -lm -o build/test-profile
	build/test-profile
build:
	mkdir -p build
	$(ZIG) cc -target x86-windows-gnu -shared -O2 $(WARN) -Wno-unused-parameter -Wl,--image-base,0x60000000 src/rain_module.c ../control-core/src/control_win.c -luser32 -ladvapi32 -o build/FrontierOrbitRain08.dll
	$(ZIG) cc -target x86-windows-gnu -O2 $(WARN) -Wl,--subsystem,windows src/studio.c src/studio_backend.c src/profile_store.c -luser32 -lgdi32 -ladvapi32 -lcomdlg32 -lshell32 -o build/FrontierOrbitStudio.exe
fixtures:
	mkdir -p build
	$(ZIG) cc -target x86-windows-gnu -O2 -UNDEBUG $(WARN) tests/studio_driver.c -luser32 -lgdi32 -lshell32 -o build/StudioDriver.exe
	python3 ../control-core/tests/make_fixture_guards.py
	$(ZIG) cc -target x86-windows-gnu -O2 -UNDEBUG $(WARN) tests/fake_rain.c -luser32 -o build/client.exe
	$(ZIG) cc -target x86-windows-gnu -shared -O2 $(WARN) -Wl,--image-base,0x50000000 ../control-core/tests/fake_d3dx.c ../control-core/tests/fake_d3dx.def -o build/d3dx9_43.dll
