package := wayland
$(package)_version := 1.20.0
$(package)_download_path := https://wayland.freedesktop.org/releases
$(package)_file_name := wayland-$($(package)_version).tar.xz
$(package)_sha256_hash := b8a034154c7059772e0fdbd27dbfcda6c732df29cae56a82274f6ec5d7cd8725

define $(package)_config_cmds
  export PKG_CONFIG_WAYLAND_SCANNER_WAYLAND_SCANNER=$$$$(env -u PKG_CONFIG_LIBDIR PKG_CONFIG_PATH=$(SYSTEM_PKG_CONFIG_PATH) pkg-config --variable=wayland_scanner wayland-scanner) && \
  meson -Dscanner=false -Dtests=false -Ddocumentation=false -Ddtd_validation=false build/ --prefix=/
endef

define $(package)_build_cmds
  ninja -C build/
endef

define $(package)_stage_cmds
  DESTDIR=$($(package)_staging_dir) ninja -C build/ install
endef
