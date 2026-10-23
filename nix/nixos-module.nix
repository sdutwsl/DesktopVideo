# NixOS module: installs videowallpaper and (optionally) starts it with every graphical session.
{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.videowallpaper;
  common = import ./service.nix;
in
{
  options.programs.videowallpaper = {
    enable = lib.mkEnableOption "videowallpaper, a video desktop wallpaper for Wayland";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./package.nix { };
      defaultText = lib.literalExpression "pkgs.callPackage <videowallpaper>/nix/package.nix { }";
      description = "The videowallpaper package to use.";
    };

    autostart = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Start videowallpaper as a systemd user service with the graphical session.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    systemd.user.services.videowallpaper = lib.mkIf cfg.autostart {
      inherit (common) description;
      wantedBy = [ "graphical-session.target" ];
      partOf = [ "graphical-session.target" ];
      after = [ "graphical-session.target" ];
      serviceConfig = common.serviceConfig // {
        ExecStart = lib.getExe cfg.package;
      };
    };
  };
}
