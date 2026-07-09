.PHONY: all bootstrap-cef bootstrap-chromium build-chromium-cef sync-source-distrib slim-runtime source-distrib backend-dev build-shell build mac-build mac-install install-wrapper install benchmark benchmark-live benchmark-all key-regression vite-install vite-dev vite-build vite-preview run clean status

BUILD_DIR ?= build-source
JOBS ?= 12
INSTALL_BIN ?= $(HOME)/.local/bin/vimbrowser
INSTALL_IPC_BIN ?= $(HOME)/.local/bin/vimbrowser-ipc
INSTALL_IPC_SCREENSHOT_BIN ?= $(HOME)/.local/bin/vimbrowser-ipc-screenshot
INSTALL_XDG_BIN ?= $(HOME)/.local/bin/vimbrowser-xdg-open
INSTALL_DESKTOP ?= $(HOME)/.local/share/applications/vimbrowser.desktop
INSTALL_ICON ?= $(HOME)/.local/share/icons/vimbrowser.png
WRAPPER_PROFILE_DIR ?= /home/yeyito/.runtime/vimbrowser-yeyito
SOURCE_CEF_ROOT ?= $(shell ls -d $(CURDIR)/backend/chromium/cef/binary_distrib/cef_binary_*_linux64_minimal 2>/dev/null | tail -n 1)
CEF_ROOT ?= $(SOURCE_CEF_ROOT)
BENCH_BINARY ?= $(abspath $(BUILD_DIR))/Release/vimbrowser
MAC_BUILD_DIR ?= build-mac.noindex
MAC_CEF_ROOT ?= $(CURDIR)/third_party/cef-mac
MAC_APP ?= $(abspath $(MAC_BUILD_DIR))/Release/vimbrowser.app
MAC_INSTALL_APP ?= $(HOME)/Applications/vimbrowser.app
MAC_PROFILE_DIR ?= $(HOME)/.runtime/vimbrowser-$(USER)

all: build

bootstrap-cef:
	./scripts/bootstrap-cef.sh

bootstrap-chromium:
	./scripts/bootstrap-chromium-source.sh

build-chromium-cef:
	./scripts/build-chromium-cef.sh

sync-source-distrib:
	./scripts/sync-chromium-cef-distrib.sh

slim-runtime:
	./scripts/slim-cef-runtime.sh "$(abspath $(BUILD_DIR))/Release"

source-distrib:
	cd backend/chromium && PATH="$(CURDIR)/backend/depot_tools:$$PATH" autoninja -C out/Release_GN_x64 chrome_sandbox
	cd backend/chromium/cef/tools && ./make_distrib.sh --ninja-build --x64-build --minimal --allow-partial --no-archive --output-dir ../binary_distrib
	./scripts/slim-cef-runtime.sh "$$(ls -d backend/chromium/cef/binary_distrib/cef_binary_*_linux64_minimal 2>/dev/null | tail -n 1)"

backend-dev:
	./scripts/backend-dev-build.sh "$(abspath $(BUILD_DIR))" "$(CEF_ROOT)" "$(JOBS)"

build-shell:
	@test -n "$(CEF_ROOT)" || { echo 'No source-built CEF distribution found; run make build-chromium-cef source-distrib, or set CEF_ROOT'; exit 1; }
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT="$(CEF_ROOT)"
	cmake --build $(BUILD_DIR) -j$(JOBS)
	./scripts/slim-cef-runtime.sh "$(abspath $(BUILD_DIR))/Release"

build: backend-dev

mac-build: bootstrap-cef
	cmake -S . -B "$(MAC_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release -DCEF_ROOT="$(MAC_CEF_ROOT)"
	cmake --build "$(MAC_BUILD_DIR)" -j$(JOBS)

mac-install: mac-build
	mkdir -p "$(dir $(MAC_INSTALL_APP))" "$(dir $(INSTALL_BIN))"
	rm -rf "$(MAC_INSTALL_APP)"
	ditto "$(MAC_APP)" "$(MAC_INSTALL_APP)"
	printf '%s\n' '#!/usr/bin/env bash' \
	  'set -euo pipefail' \
	  'log_dir="$${XDG_CACHE_HOME:-$$HOME/.cache}/vimbrowser"' \
	  'profile_dir="$${VIMBROWSER_PROFILE_DIR:-$(MAC_PROFILE_DIR)}"' \
	  'mkdir -p "$$log_dir" "$$profile_dir"' \
	  'exec "$(MAC_INSTALL_APP)/Contents/MacOS/vimbrowser" --profile-dir="$$profile_dir" "$$@" >> "$$log_dir/vimbrowser.log" 2>&1' > "$(INSTALL_BIN)"
	chmod +x "$(INSTALL_BIN)"
	cp "$(abspath $(MAC_BUILD_DIR))/Release/vimbrowser-ipc" "$(INSTALL_IPC_BIN)"
	chmod +x "$(INSTALL_IPC_BIN)"
	@if [ -x /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister ]; then \
	  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$(MAC_INSTALL_APP)"; \
	fi
	@echo 'installed $(MAC_INSTALL_APP)'
	@echo 'installed $(INSTALL_BIN) and $(INSTALL_IPC_BIN)'

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
	@if [ -x "$(abspath $(BUILD_DIR))/Release/vimbrowser-ipc" ]; then \
	  rm -f $(INSTALL_IPC_BIN); \
	  cp "$(abspath $(BUILD_DIR))/Release/vimbrowser-ipc" $(INSTALL_IPC_BIN); \
	  chmod +x $(INSTALL_IPC_BIN); \
	  cp scripts/vimbrowser-ipc-screenshot $(INSTALL_IPC_SCREENSHOT_BIN); \
	  chmod +x $(INSTALL_IPC_SCREENSHOT_BIN); \
	fi
	mkdir -p $(dir $(INSTALL_XDG_BIN)) $(dir $(INSTALL_DESKTOP)) $(dir $(INSTALL_ICON))
	cp assets/vimbrowser.png $(INSTALL_ICON)
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
	  'Icon=$(INSTALL_ICON)' \
	  'Categories=Network;WebBrowser;' \
	  'StartupNotify=true' \
	  'MimeType=x-scheme-handler/unknown;x-scheme-handler/about;text/html;text/xml;application/xhtml+xml;application/xml;application/rdf+xml;application/pdf;image/gif;image/jpeg;image/png;image/webp;video/mp4;x-scheme-handler/http;x-scheme-handler/https;' > $(INSTALL_DESKTOP)
	@if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database $(dir $(INSTALL_DESKTOP)); fi
	@echo 'installed $(INSTALL_BIN) -> $(abspath $(BUILD_DIR))/Release/vimbrowser'
	@if [ -x "$(INSTALL_IPC_BIN)" ]; then echo 'installed $(INSTALL_IPC_BIN)'; fi
	@echo 'installed $(INSTALL_DESKTOP)'

install: build
	$(MAKE) install-wrapper

benchmark:
	./scripts/vimbrowser-benchmark --suite local --check --binary "$(BENCH_BINARY)"

benchmark-live:
	./scripts/vimbrowser-benchmark --suite live --binary "$(BENCH_BINARY)"

benchmark-all:
	./scripts/vimbrowser-benchmark --suite all --check --binary "$(BENCH_BINARY)"

key-regression:
	./scripts/vimbrowser-key-regression --binary "$(BENCH_BINARY)"

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
