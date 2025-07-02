{
  description = "ESP dev-shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    esp-dev.url = "github:mirrexagon/nixpkgs-esp-dev";
    esp-dev.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = { nixpkgs, esp-dev, ... }:
    let
      system = "x86_64-linux";
      esp-overlay = import "${esp-dev}/overlay.nix";

      pkgs = import nixpkgs {
        inherit system;
        overlays = [ esp-overlay ];
      };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        name = "esp32-s3-project";

        buildInputs = with pkgs; [
          esp-idf-esp32s3
          clang-tools
        ];
      };
    };
}
