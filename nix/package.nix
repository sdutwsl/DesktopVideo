{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  wayland-scanner,
  mpv-unwrapped,
  wayland,
  libglvnd,
  libffi,
  sdbus-cpp,
  systemdLibs,
}:

stdenv.mkDerivation {
  pname = "videowallpaper";
  version = "2.0";

  # Only what the build reads, so README/flake/CI edits do not trigger a rebuild.
  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../cmake
      ../data
      ../src/core
      ../src/wayland
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = [
    mpv-unwrapped # libmpv
    wayland
    libglvnd # EGL + GLESv2
    libffi # wayland-client.pc depends on it
    sdbus-cpp
    systemdLibs # sd-bus, required by sdbus-cpp
  ];

  meta = {
    description = "Video desktop wallpaper for Wayland compositors with wlr-layer-shell (KDE Plasma, Sway, Hyprland, ...)";
    homepage = "https://github.com/sdutwsl/DesktopVideo";
    platforms = lib.platforms.linux;
    mainProgram = "videowallpaper";
  };
}
