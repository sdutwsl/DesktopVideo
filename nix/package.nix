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
  electron,
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
  # The yt-dlp application is not a Python module; the session importer needs
  # the library (and its keyring dependencies) at the same version.
  sessionPython = python3.withPackages (ps: [
    (ps.yt-dlp.overridePythonAttrs (_old: { inherit (youtubeDlp) version src; }))
    ps.pycryptodomex
  ]);
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
      ../desktop
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner
    makeWrapper
  ];

  buildInputs = [
    mpv-unwrapped # libmpv
    wayland
    libglvnd # EGL + GLESv2
    libffi # wayland-client.pc depends on it
    sdbus-cpp_2
    systemdLibs # sd-bus, required by sdbus-cpp
  ];

  doCheck = true;
  postInstall = ''
    mkdir -p $out/share/muralop/desktop
    cp $src/desktop/{*.cjs,*.html,*.css,package.json} $out/share/muralop/desktop/
    cp $src/gui/session.py $out/share/muralop/
  '';
  postFixup = ''
    makeWrapper ${electron}/bin/electron $out/bin/muralop-ui \
      --add-flags "$out/share/muralop/desktop" \
      --add-flags "--backend $out/bin/muralop" \
      --prefix PATH : ${lib.makeBinPath [ youtubeDlp deno ]} \
      --set MURALOP_YTDLP ${youtubeDlp}/bin/yt-dlp \
      --set MURALOP_SESSION_HELPER $out/bin/muralop-session
    makeWrapper ${sessionPython}/bin/python $out/bin/muralop-session \
      --add-flags "$out/share/muralop/session.py"
    wrapProgram $out/bin/muralop --prefix PATH : ${lib.makeBinPath [ youtubeDlp deno ]}
  '';

  meta = {
    description = "Video desktop wallpaper for Wayland compositors with wlr-layer-shell (KDE Plasma, Sway, Hyprland, ...)";
    homepage = "https://github.com/sdutwsl/Muralop";
    platforms = lib.platforms.linux;
    mainProgram = "muralop";
  };
}
