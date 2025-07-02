{
  description = "ESP dev-shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
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

      idf = esp-dev.packages.${system}.esp-idf-esp32s3.override (final: {
        toolsToInclude = final.toolsToInclude ++ [
          "esp-clang"
        ];
      });
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        name = "esp32-s3-project";
        buildInputs = with pkgs; [
          idf
        ];
        shellHook = ''
          export CLANGD_QUERY_DRIVER=`which xtensa-esp32-elf-g++`
        '';
      };
    };
}
