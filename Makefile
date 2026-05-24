PLUGIN_BUILD_DIR ?= build
PLUGIN_CONFIG_PRESET ?= dev
PLUGIN_CONFIG_PRESET_RELEASE ?= dev-release
PLUGIN_SO := $(PLUGIN_BUILD_DIR)/fyplugin_vimmotions.so

ifneq (,$(wildcard /fooyin/CMakeLists.txt))
FOOYIN_SRC_DIR ?= /fooyin
else
FOOYIN_SRC_DIR ?= $(abspath ../fooyin)
endif

FOOYIN_BUILD_DIR ?= $(FOOYIN_SRC_DIR)/build
FOOYIN_PLUGIN_DIR ?= $(FOOYIN_BUILD_DIR)/run/lib/fooyin/plugins
FOOYIN_BIN ?= $(firstword \
	$(wildcard $(FOOYIN_BUILD_DIR)/run/fooyin) \
	$(wildcard $(FOOYIN_BUILD_DIR)/run/bin/fooyin))

QT_LOGGING_RULES ?= fy.vim.debug=true
XDG_CONFIG_HOME ?=

.PHONY: help fooyin-configure fooyin-build configure configure-release build build-release \
	install-plugin sync-plugin run run-debug dev dev-release

help:
	@printf '%s\n' \
	  'Targets:' \
	  '  make fooyin-configure  Configure fooyin into $(FOOYIN_BUILD_DIR)' \
	  '  make fooyin-build      Build fooyin in $(FOOYIN_BUILD_DIR)' \
	  '  make configure         Configure plugin with preset $(PLUGIN_CONFIG_PRESET)' \
	  '  make build             Build plugin into $(PLUGIN_BUILD_DIR)' \
	  '  make install-plugin    Copy plugin into $(FOOYIN_PLUGIN_DIR)' \
	  '  make run               Launch fooyin with the copied plugin' \
	  '  make run-debug         Launch fooyin with fy.vim debug logging enabled' \
	  '  make dev               Build fooyin, build plugin, copy plugin' \
	  '' \
	  'Overrides:' \
	  '  FOOYIN_SRC_DIR=/path/to/fooyin' \
	  '  FOOYIN_BUILD_DIR=/path/to/fooyin/build' \
	  '  FOOYIN_BIN=/path/to/fooyin/binary' \
	  '  XDG_CONFIG_HOME=/tmp/fooyin-dev-config'

fooyin-configure:
	cmake -S "$(FOOYIN_SRC_DIR)" -B "$(FOOYIN_BUILD_DIR)" -G Ninja -DBUILD_TRANSLATIONS=OFF

fooyin-build:
	cmake --build "$(FOOYIN_BUILD_DIR)"

configure:
	cmake --preset "$(PLUGIN_CONFIG_PRESET)"

configure-release:
	cmake --preset "$(PLUGIN_CONFIG_PRESET_RELEASE)"

build:
	cmake --build "$(PLUGIN_BUILD_DIR)"

build-release:
	cmake --build "$(PLUGIN_BUILD_DIR)" --config RelWithDebInfo

install-plugin: $(PLUGIN_SO)
	mkdir -p "$(FOOYIN_PLUGIN_DIR)"
	cp "$(PLUGIN_SO)" "$(FOOYIN_PLUGIN_DIR)/"

sync-plugin: install-plugin

run:
	@if [ -z "$(FOOYIN_BIN)" ]; then \
		printf '%s\n' 'Set FOOYIN_BIN to your fooyin executable (for example make run FOOYIN_BIN=/path/to/fooyin).' >&2; \
		exit 1; \
	fi
	env XDG_CONFIG_HOME="$(XDG_CONFIG_HOME)" "$(FOOYIN_BIN)"

run-debug:
	@if [ -z "$(FOOYIN_BIN)" ]; then \
		printf '%s\n' 'Set FOOYIN_BIN to your fooyin executable (for example make run-debug FOOYIN_BIN=/path/to/fooyin).' >&2; \
		exit 1; \
	fi
	env QT_LOGGING_RULES="$(QT_LOGGING_RULES)" XDG_CONFIG_HOME="$(XDG_CONFIG_HOME)" "$(FOOYIN_BIN)"

dev: fooyin-configure fooyin-build configure build install-plugin

dev-release: fooyin-configure fooyin-build configure-release build install-plugin
