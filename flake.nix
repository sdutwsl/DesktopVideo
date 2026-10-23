{
  description = "VideoWallpaper - video desktop wallpaper for Windows and Wayland";

  # Resolved through the flake registry (system nixpkgs on NixOS); override with
  #   nix develop --override-input nixpkgs github:NixOS/nixpkgs/nixos-unstable
  inputs.nixpkgs.url = "flake:nixpkgs";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      waylandDeps = pkgs: with pkgs; [
        libvlc
        wayland
        wayland-protocols
        wlr-protocols
        libglvnd          # EGL + GLESv2
        libffi            # wayland-client.pc depends on it
        sdbus-cpp
        systemdLibs       # sd-bus, required by sdbus-cpp
      ];
      nativeDeps = pkgs: with pkgs; [
        cmake
        ninja
        pkg-config
        wayland-scanner
      ];
    in
    {
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "videowallpaper";
          version = "2.0";
          src = nixpkgs.lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              let name = baseNameOf path;
              in !(nixpkgs.lib.hasPrefix "build" name || name == "Archived" || name == "result");
          };
          nativeBuildInputs = nativeDeps pkgs;
          buildInputs = waylandDeps pkgs;
        };
      });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = nativeDeps pkgs;
          buildInputs = waylandDeps pkgs;
        };
        # Cross-compile the Windows exe: nix develop .#mingw, then use cmake/mingw-toolchain.cmake
        mingw = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [ cmake ninja pkgsCross.mingwW64.stdenv.cc ];
          buildInputs = [ pkgs.pkgsCross.mingwW64.windows.mcfgthreads ];
        };
      });
    };
}
