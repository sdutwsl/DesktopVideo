# home-manager module: same as the NixOS one, but per user.
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
    home.packages = [ cfg.package ];

    systemd.user.services.videowallpaper = lib.mkIf cfg.autostart {
      Unit = {
        Description = common.description;
        PartOf = [ "graphical-session.target" ];
        After = [ "graphical-session.target" ];
      };
      Service = common.serviceConfig // {
        ExecStart = lib.getExe cfg.package;
      };
      Install.WantedBy = [ "graphical-session.target" ];
    };
  };
}
