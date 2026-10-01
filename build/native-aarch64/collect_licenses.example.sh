#!/bin/bash
# EXAMPLE (3D Movie Maker): one LICENSE.<component>.txt per shipped component, from the upstream sources.
# Copy the upstream license of every shipped component into release/3dmm/licenses (run in the chroot).
set -e
L=/work/release/3dmm/licenses
S=/root/depsrc
D=/root/3dmm-build/_deps
rm -rf $L && mkdir -p $L
cp /work/tools-src/3DMMEx/LICENSE                     $L/LICENSE.3dmmex.txt
cp $D/3dmm-brender-src/LICENSE                        $L/LICENSE.brender.txt
cp $D/iniparser-src/LICENSE                           $L/LICENSE.iniparser.txt
cp $D/miniaudio-src/LICENSE                           $L/LICENSE.miniaudio.txt
cp $S/SDL_ttf/LICENSE.txt                             $L/LICENSE.sdl2_ttf.txt
cp $S/SDL_ttf/external/freetype/docs/FTL.TXT          $L/LICENSE.freetype.txt
cp $S/fluidsynth/LICENSE                              $L/LICENSE.fluidsynth.txt
cp $S/libsndfile/COPYING                              $L/LICENSE.libsndfile.txt
cp $S/ogg/COPYING                                     $L/LICENSE.libogg.txt
cp $S/vorbis/COPYING                                  $L/LICENSE.libvorbis.txt
cp $S/flac/COPYING.Xiph                               $L/LICENSE.flac.txt
cp $S/opus/COPYING                                    $L/LICENSE.opus.txt
cp /work/tools-src/OmniOSK/LICENSE                    $L/LICENSE.omniosk.txt
cp /work/tools-src/OmniOSK/third_party/imgui/LICENSE.txt $L/LICENSE.imgui.txt
cp /work/pctest/comicneue-src/OFL.txt                 $L/LICENSE.comicneue.txt
cp /work/pctest/soundfont/LICENSE.txt                 $L/LICENSE.generaluser-gs.txt
ls -la $L
