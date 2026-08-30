{
  description = "Click Between Frames - Linux evdev input helper build";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }: let
    system = "x86_64-linux";
    pkgs = import nixpkgs { inherit system; };
    p = pkgs.pkgsStatic;
  in {
    packages.${system}.linux-input = p.stdenv.mkDerivation {
      pname = "linux-input";
      version = "1.0.0";
      src = ./src/linux;

      nativeBuildInputs = [ p.gcc13Stdenv.cc p.pkg-config ];
      buildInputs = [ p.libevdev.dev ];

      buildPhase = ''
        g++ -O2 -static -o linux-input.so linux-input.cpp $(pkg-config --cflags --libs libevdev)
      '';

      installPhase = ''
        mkdir -p $out
        cp linux-input.so $out/
      '';
    };

    packages.${system}.default = self.packages.${system}.linux-input;
  };
}