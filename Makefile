.PHONY: bootstrap configure build install run clean status

BUILD_DIR ?= build
JOBS ?= 12
INSTALL_BIN ?= $(HOME)/.local/bin/vimbrowser

bootstrap:
	./scripts/bootstrap-cef.sh

configure: bootstrap
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

install: build
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
