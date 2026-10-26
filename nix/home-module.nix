# home-manager module: same as the NixOS one, but per user.
{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.muralop;
  common = import ./service.nix;
in
{
  options.programs.muralop = {
    enable = lib.mkEnableOption "muralop, a video desktop wallpaper for Wayland";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./package.nix { };
      defaultText = lib.literalExpression "pkgs.callPackage <muralop>/nix/package.nix { }";
      description = "The muralop package to use.";
    };

    autostart = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Start muralop as a systemd user service with the graphical session.";
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = [ cfg.package ];

    systemd.user.services.muralop = lib.mkIf cfg.autostart {
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
