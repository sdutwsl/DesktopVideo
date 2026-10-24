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
  python3,
  yt-dlp,
  qt6,
  makeWrapper,
}:

let
  guiPython = python3.withPackages (ps: [ ps.pyside6 ps.yt-dlp ps.pycryptodomex ]);
in stdenv.mkDerivation {
  pname = "videowallpaper";
  # project(... VERSION x.y.z) in CMakeLists.txt is the only place the version is set.
  version = builtins.head (
    builtins.match ".*project\\(VideoWallpaper VERSION ([0-9.]+).*" (builtins.readFile ../CMakeLists.txt)
  );

  # Only what the build reads, so README/flake/CI edits do not trigger a rebuild.
  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../cmake
      ../data
      ../src/core
      ../src/wayland
      ../tests
      ../gui
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner
    qt6.wrapQtAppsHook
    makeWrapper
  ];

  buildInputs = [
    mpv-unwrapped # libmpv
    wayland
    libglvnd # EGL + GLESv2
    libffi # wayland-client.pc depends on it
    sdbus-cpp
    systemdLibs # sd-bus, required by sdbus-cpp
    qt6.qtwebengine
    qt6.qtbase
  ];

  doCheck = true;
  dontWrapQtApps = true;
  postInstall = ''
    mkdir -p $out/share/videowallpaper/gui
    cp $src/gui/*.py $out/share/videowallpaper/gui/
  '';
  postFixup = ''
    makeWrapper ${guiPython}/bin/python $out/bin/videowallpaper-ui \
      --add-flags "$out/share/videowallpaper/gui/main.py" \
      --add-flags "--backend $out/bin/videowallpaper" \
      --prefix PATH : ${lib.makeBinPath [ yt-dlp ]} \
      "''${qtWrapperArgs[@]}"
    wrapProgram $out/bin/videowallpaper --prefix PATH : ${lib.makeBinPath [ yt-dlp ]}
  '';

  meta = {
    description = "Video desktop wallpaper for Wayland compositors with wlr-layer-shell (KDE Plasma, Sway, Hyprland, ...)";
    homepage = "https://github.com/sdutwsl/DesktopVideo";
    platforms = lib.platforms.linux;
    mainProgram = "videowallpaper";
  };
}
