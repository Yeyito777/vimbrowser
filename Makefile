.PHONY: bootstrap bootstrap-chromium build-chromium-cef source-distrib configure configure-source build build-source install-wrapper install install-source run clean status

BUILD_DIR ?= build
SOURCE_BUILD_DIR ?= build-source
JOBS ?= 12
INSTALL_BIN ?= $(HOME)/.local/bin/vimbrowser
SOURCE_CEF_ROOT ?= $(shell ls -d $(CURDIR)/third_party/chromium/src/cef/binary_distrib/cef_binary_*_linux64_minimal 2>/dev/null | tail -n 1)
CMAKE_ARGS ?=
ifneq ($(CEF_ROOT),)
CMAKE_ARGS += -DCEF_ROOT=$(CEF_ROOT)
endif

bootstrap:
	./scripts/bootstrap-cef.sh

bootstrap-chromium:
	./scripts/bootstrap-chromium-source.sh

build-chromium-cef:
	./scripts/build-chromium-cef.sh

source-distrib:
	cd third_party/chromium/src && PATH="$(CURDIR)/third_party/chromium/depot_tools:$$PATH" autoninja -C out/Release_GN_x64 chrome_sandbox
	cd third_party/chromium/src/cef/tools && ./make_distrib.sh --ninja-build --x64-build --minimal --allow-partial --no-archive --output-dir ../binary_distrib

configure-source:
	@test -n "$(or $(CEF_ROOT),$(SOURCE_CEF_ROOT))" || { echo 'No source-built CEF distribution found; run make build-chromium-cef source-distrib, or set CEF_ROOT'; exit 1; }
	cmake -S . -B $(SOURCE_BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT="$(or $(CEF_ROOT),$(SOURCE_CEF_ROOT))"

configure: bootstrap
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

build-source: configure-source
	cmake --build $(SOURCE_BUILD_DIR) -j$(JOBS)

install-wrapper:
	mkdir -p $(dir $(INSTALL_BIN))
	rm -f $(INSTALL_BIN)
	printf '%s\n' '#!/usr/bin/env bash' \
	  'set -euo pipefail' \
	  'log_dir="$${XDG_CACHE_HOME:-$$HOME/.cache}/vimbrowser"' \
	  'mkdir -p "$$log_dir"' \
	  'log_file="$$log_dir/vimbrowser.log"' \
	  'printf "\\n[%s] vimbrowser %q\\n" "$$(date --iso-8601=seconds)" "$$*" >> "$$log_file"' \
	  'cd "$(abspath $(BUILD_DIR))/Release"' \
	  'exec ./vimbrowser "$$@" >> "$$log_file" 2>&1' > $(INSTALL_BIN)
	chmod +x $(INSTALL_BIN)
	@echo 'installed $(INSTALL_BIN) -> $(abspath $(BUILD_DIR))/Release/vimbrowser'

install: build install-wrapper

install-source: build-source
	$(MAKE) BUILD_DIR=$(SOURCE_BUILD_DIR) install-wrapper

run: build
	$(abspath $(BUILD_DIR))/Release/vimbrowser --disable-gpu https://example.com

clean:
	rm -rf $(BUILD_DIR)

status:
	@echo repo: $(CURDIR)
	@echo build: $(abspath $(BUILD_DIR))/Release/vimbrowser
	@test -x $(BUILD_DIR)/Release/vimbrowser && echo built=yes || echo built=no
	@echo install: $(INSTALL_BIN)
	@test -e $(INSTALL_BIN) && readlink -f $(INSTALL_BIN) || true
