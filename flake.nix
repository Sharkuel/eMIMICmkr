{
  description = "eMIMICmkr: record drum plugin kits and build Pearl Mimic instruments";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forAll (pkgs: rec {
        emimicmkr = pkgs.callPackage ./packaging/nix/package.nix { src = self; };
        default = emimicmkr;
      });
      apps = forAll (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.default}/bin/eMIMICmkr";
        };
      });
    };
}
