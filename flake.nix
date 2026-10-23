{
  description = "VideoWallpaper - video desktop wallpaper for Windows and Wayland";

  # Resolved through the flake registry (system nixpkgs on NixOS); override with
  #   nix develop --override-input nixpkgs github:NixOS/nixpkgs/nixos-unstable
  # Consumers should use `inputs.nixpkgs.follows = "nixpkgs"` so the binary links the
  # same glibc as the system's GPU drivers (/run/opengl-driver).
  inputs.nixpkgs.url = "flake:nixpkgs";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAll (pkgs: rec {
        videowallpaper = pkgs.callPackage ./nix/package.nix { };
        default = videowallpaper;
      });

      overlays.default = final: _prev: {
        videowallpaper = final.callPackage ./nix/package.nix { };
      };

      # programs.videowallpaper.{enable,package,autostart}
      nixosModules.default = import ./nix/nixos-module.nix;
      homeModules.default = import ./nix/home-module.nix;
      homeManagerModules.default = self.homeModules.default;

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
        };
        # Cross-compile the Windows exe: nix develop .#mingw, then use cmake/mingw-toolchain.cmake
        mingw = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            p7zip # unpack the libmpv SDK (mpv-dev-x86_64-*.7z)
            pkgsCross.mingwW64.stdenv.cc
          ];
          buildInputs = [ pkgs.pkgsCross.mingwW64.windows.mcfgthreads ];
        };
      });

      checks = forAll (pkgs: {
        package = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      });
    };
}
