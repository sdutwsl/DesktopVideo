# Shared by the NixOS and home-manager modules.
{
  description = "Video desktop wallpaper";
  serviceConfig = {
    # Exits 0 when the compositor goes away (logout), so only crashes are restarted.
    Restart = "on-failure";
    RestartSec = 3;
  };
}
