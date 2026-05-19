.PHONY: bootstrap bootstrap-chromium build-chromium-cef sync-source-distrib slim-runtime backend-dev source-distrib configure configure-source build build-source install-wrapper install install-source vite-install vite-dev vite-build vite-preview run clean status

BUILD_DIR ?= build
SOURCE_BUILD_DIR ?= build-source
JOBS ?= 12
INSTALL_BIN ?= $(HOME)/.local/bin/vimbrowser
INSTALL_XDG_BIN ?= $(HOME)/.local/bin/vimbrowser-xdg-open
INSTALL_DESKTOP ?= $(HOME)/.local/share/applications/vimbrowser.desktop
WRAPPER_PROFILE_DIR ?= /home/yeyito/.runtime/vimbrowser-yeyito
SOURCE_CEF_ROOT ?= $(shell ls -d $(CURDIR)/backend/chromium/cef/binary_distrib/cef_binary_*_linux64_minimal 2>/dev/null | tail -n 1)
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

sync-source-distrib:
	./scripts/sync-chromium-cef-distrib.sh

slim-runtime:
	./scripts/slim-cef-runtime.sh "$(or $(CEF_ROOT),$(SOURCE_CEF_ROOT))" "$(abspath $(BUILD_DIR))/Release"

backend-dev: build-chromium-cef sync-source-distrib configure-source
	cmake --build $(SOURCE_BUILD_DIR) -j$(JOBS)
	./scripts/slim-cef-runtime.sh "$(abspath $(SOURCE_BUILD_DIR))/Release"
	$(MAKE) BUILD_DIR=$(SOURCE_BUILD_DIR) install-wrapper

source-distrib:
	cd backend/chromium && PATH="$(CURDIR)/backend/depot_tools:$$PATH" autoninja -C out/Release_GN_x64 chrome_sandbox
	cd backend/chromium/cef/tools && ./make_distrib.sh --ninja-build --x64-build --minimal --allow-partial --no-archive --output-dir ../binary_distrib
	./scripts/slim-cef-runtime.sh "$$(ls -d backend/chromium/cef/binary_distrib/cef_binary_*_linux64_minimal 2>/dev/null | tail -n 1)"

configure-source:
	@test -n "$(or $(CEF_ROOT),$(SOURCE_CEF_ROOT))" || { echo 'No source-built CEF distribution found; run make build-chromium-cef source-distrib, or set CEF_ROOT'; exit 1; }
	cmake -S . -B $(SOURCE_BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT="$(or $(CEF_ROOT),$(SOURCE_CEF_ROOT))"

configure: bootstrap
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)
	./scripts/slim-cef-runtime.sh "$(abspath $(BUILD_DIR))/Release"

build-source: configure-source
	cmake --build $(SOURCE_BUILD_DIR) -j$(JOBS)
	./scripts/slim-cef-runtime.sh "$(abspath $(SOURCE_BUILD_DIR))/Release"

install-wrapper:
	mkdir -p $(dir $(INSTALL_BIN))
	rm -f $(INSTALL_BIN)
	printf '%s\n' '#!/usr/bin/env bash' \
	  'set -euo pipefail' \
	  'launch_cwd="$$PWD"' \
	  'log_dir="$${XDG_CACHE_HOME:-$$HOME/.cache}/vimbrowser"' \
	  'mkdir -p "$$log_dir"' \
	  'log_file="$$log_dir/vimbrowser.log"' \
	  'printf "\\n[%s] vimbrowser %q\\n" "$$(date --iso-8601=seconds)" "$$*" >> "$$log_file"' \
	  'cd "$(abspath $(BUILD_DIR))/Release"' \
	  'exec env VIMBROWSER_LAUNCH_CWD="$$launch_cwd" ./vimbrowser --profile-dir="$(WRAPPER_PROFILE_DIR)" "$$@" >> "$$log_file" 2>&1' > $(INSTALL_BIN)
	chmod +x $(INSTALL_BIN)
	mkdir -p $(dir $(INSTALL_XDG_BIN)) $(dir $(INSTALL_DESKTOP))
	rm -f $(INSTALL_XDG_BIN)
	printf '%s\n' '#!/usr/bin/env bash' \
	  'set -euo pipefail' \
	  '# Detach desktop/XDG launches so xdg-open returns immediately.' \
	  '# The main launcher logs output and forwards URLs/files to an already-open profile.' \
	  'nohup "$(INSTALL_BIN)" "$$@" >/dev/null 2>&1 &' > $(INSTALL_XDG_BIN)
	chmod +x $(INSTALL_XDG_BIN)
	printf '%s\n' '[Desktop Entry]' \
	  'Name=vimbrowser' \
	  'GenericName=Web Browser' \
	  'Comment=Custom native CEF/Chromium vim-like browser' \
	  'Exec=$(INSTALL_XDG_BIN) %u' \
	  'Terminal=false' \
	  'Type=Application' \
	  'Icon=web-browser' \
	  'Categories=Network;WebBrowser;' \
	  'StartupNotify=true' \
	  'MimeType=x-scheme-handler/unknown;x-scheme-handler/about;text/html;text/xml;application/xhtml+xml;application/xml;application/rdf+xml;application/pdf;image/gif;image/jpeg;image/png;image/webp;video/mp4;x-scheme-handler/http;x-scheme-handler/https;' > $(INSTALL_DESKTOP)
	@if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database $(dir $(INSTALL_DESKTOP)); fi
	@echo 'installed $(INSTALL_BIN) -> $(abspath $(BUILD_DIR))/Release/vimbrowser'
	@echo 'installed $(INSTALL_DESKTOP)'

install: build install-wrapper

install-source: build-source
	$(MAKE) BUILD_DIR=$(SOURCE_BUILD_DIR) install-wrapper

vite-install:
	npm --prefix frontend install

vite-dev:
	npm --prefix frontend run dev

vite-build:
	npm --prefix frontend run build

vite-preview:
	npm --prefix frontend run preview

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
