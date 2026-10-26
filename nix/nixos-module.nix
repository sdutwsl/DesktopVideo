# NixOS module: installs muralop and (optionally) starts it with every graphical session.
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
    environment.systemPackages = [ cfg.package ];

    systemd.user.services.muralop = lib.mkIf cfg.autostart {
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
