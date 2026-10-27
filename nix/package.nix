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
  sdbus-cpp_2,
  systemdLibs,
  python3,
  yt-dlp,
  fetchFromGitHub,
  deno,
  qt6,
  makeWrapper,
}:

let
  # Match the tested portable bundle while nixpkgs is behind YouTube changes.
  youtubeDlp = yt-dlp.overrideAttrs (_old: {
    version = "2026.10.21";
    src = fetchFromGitHub {
      owner = "yt-dlp";
      repo = "yt-dlp";
      rev = "2026.10.21";
      sha256 = "0s7ay24d2d6glijsgx42kbn61x46s9h69h4v6sp6l6dvzawm180b";
    };
  });
  guiPython = python3.withPackages (ps: [ ps.pyside6 youtubeDlp ps.pycryptodomex ]);
in
stdenv.mkDerivation {
  pname = "muralop";
  # project(... VERSION x.y.z) in CMakeLists.txt is the only place the version is set.
  version = builtins.head (
    builtins.match ".*project\\(Muralop VERSION ([0-9.]+).*" (builtins.readFile ../CMakeLists.txt)
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
    sdbus-cpp_2
    systemdLibs # sd-bus, required by sdbus-cpp
    qt6.qtwebengine
    qt6.qtbase
  ];

  doCheck = true;
  dontWrapQtApps = true;
  postInstall = ''
    mkdir -p $out/share/muralop/gui
    cp $src/gui/*.py $out/share/muralop/gui/
  '';
  postFixup = ''
    makeWrapper ${guiPython}/bin/python $out/bin/muralop-ui \
      --add-flags "$out/share/muralop/gui/main.py" \
      --add-flags "--backend $out/bin/muralop" \
      --prefix PATH : ${lib.makeBinPath [ youtubeDlp deno ]} \
      --unset QT_PLUGIN_PATH \
      --unset QT_QPA_PLATFORMTHEME \
      --unset QT_STYLE_OVERRIDE \
      "''${qtWrapperArgs[@]}"
    wrapProgram $out/bin/muralop --prefix PATH : ${lib.makeBinPath [ youtubeDlp deno ]}
  '';

  meta = {
    description = "Video desktop wallpaper for Wayland compositors with wlr-layer-shell (KDE Plasma, Sway, Hyprland, ...)";
    homepage = "https://github.com/sdutwsl/Muralop";
    platforms = lib.platforms.linux;
    mainProgram = "muralop";
  };
}
