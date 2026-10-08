# eMIMICmkr for Nix and NixOS (the flake at the repository root builds it).
{ lib, stdenv, src, fetchFromGitHub, cmake, ninja, pkg-config, makeWrapper
, alsa-lib, freetype, fontconfig, libX11, libXext, libXrandr, libXinerama, libXcursor, libXcomposite
, tesseract, grim, wl-clipboard, xclip }:

let
  juce = fetchFromGitHub {
    owner = "juce-framework";
    repo = "JUCE";
    rev = "8.0.10";
    hash = "sha256-YSNVQ337/IAlz3mFNgJisIY5D9wPz6sFboMNwsjcGBo=";
  };
  # JUCE opens these at run time rather than linking them.
  runtimeLibs = [ alsa-lib freetype fontconfig libX11 libXext libXrandr libXinerama libXcursor libXcomposite ];
in
stdenv.mkDerivation {
  pname = "emimicmkr";
  version = "0.1.0";
  inherit src;

  nativeBuildInputs = [ cmake ninja pkg-config makeWrapper ];
  buildInputs = runtimeLibs;

  cmakeFlags = [
    "-DFETCHCONTENT_SOURCE_DIR_JUCE=${juce}"
    "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
  ];
  ninjaFlags = [ "eMIMICmkr" ];

  installPhase = ''
    runHook preInstall
    cmake --install . --component eMIMICmkr --prefix $out
    runHook postInstall
  '';

  # Capture... and Paste call these tools; tesseract reads names from screenshots.
  postFixup = ''
    wrapProgram $out/bin/eMIMICmkr \
      --prefix LD_LIBRARY_PATH : ${lib.makeLibraryPath runtimeLibs} \
      --suffix PATH : ${lib.makeBinPath [ tesseract grim wl-clipboard xclip ]}
  '';

  meta = {
    description = "Record drum plugin kits and build Pearl Mimic instruments";
    homepage = "https://github.com/Sharkuel/eMIMICmkr";
    license = lib.licenses.agpl3Only;
    platforms = lib.platforms.linux;
    mainProgram = "eMIMICmkr";
  };
}
