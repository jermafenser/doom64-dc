# Doom 64 for Dreamcast 

The biggest update yet.

Full environment normal-mapping, weapons are normal-mapped, enhanced dynamic lighting.

Lots of work went into this and I hope you all enjoy it.

You *will* have to do a tiny bit of actual work to get this going. If you don't have 30 to 45 minutes to spare, just go play on Steam and call it a day. The results are worth it though.


# playing guide

    A is attack
    B is use
    X is weapon backward
    Y is weapon forward
    combined press of X and Y brings up automap and cycles through (second press goes to line map, third press back to game)
    D-PAD/Analog Stick is move
    L trigger is strafe left (analog sensitive)
    R trigger strafe right (analog sensitive)
    START is start (bring up menu)

# build guide

**Pre-requisites**

The build is known to work on the following platforms as of the current commit:

    Debian (version?)
    Ubuntu 22.04
    Windows 11 - Cygwin 64-bit
    Windows (version?) - DreamSDK

It should work on most other Linux environments.
    
You will need a host/native GCC install and a full working Dreamcast/KallistiOS toolchain install (https://dreamcast.wiki/Getting_Started_with_Dreamcast_development).

To make streaming music work correctly, you will need a small patch to KOS. When cloning the toolchain, before building, do the following:

    git clone https://github.com/KallistiOS/KallistiOS.git /opt/toolchains/dc/kos
    cd /opt/toolchains/dc/kos
    git fetch origin pull/838/head:soundfix
    git switch soundfix

Whenever this pull request finally gets approved and merged, I will update these instructions.

**Repo contents**

Whatever the directory you cloned this github repo to is named and wherever it is located, it will be referred to in this document as

`doom64-dc`

This guide will assume that you cloned it into your home directory. 

If you need to get to the top level of the repo, it will say

    cd ~/doom64-dc

Under doom64-dc, you will find

    doom64-dc/
    -- README.md (you're reading it right now)
    -- Makefile (how it gets built)
    -- doom64_hemigen/ (the tool I used to generate and compress all normal map textures)
    -- wadtool/ (the tool that builds texture and WAD files from Doom 64 ROM)
    -- selfboot/ (all files needed to make a bootable CD image)
    ---- bump.wad (BC5-compressed normal map textures in a WAD file)
    ---- maps/ (all game map WADs dumped from Doom 64 ROM by wadtool)
	---- mus/ (all of the music tracks as 44khz stereo ADPCM)
    ------ mus*.adpcm (music tracks)
    ---- sfx/ (all of the game sfx as 22khz ADPCM WAV)
    ------ sfx_*.wav (sound effects)
    ---- tex/ (weapon bumpmaps and generated non-enemy sprite sheet)
    ------ BFGG_NRM.cmp (BC5-compressed BFG normal maps)
    ------ CHGG_NRM.cmp (BC5-compressed chaingun normal maps)
    ------ LASR_NRM.cmp (BC5-compressed laser normal maps)
    ------ PISG_NRM.cmp (BC5-compressed pistol normal maps)
    ------ PLAS_NRM.cmp (BC5-compressed plasma rifle normal maps)
    ------ PUNG_NRM.cmp (BC5-compressed fist normal maps)
    ------ SAWG_NRM.cmp (BC5-compressed chainsaw normal maps)
    ------ SHT1_NRM.cmp (BC5-compressed shotgun normal maps)
    ------ LASR_NRM.cmp (BC5-compressed super shotgun normal maps)
    ------ WEPN_DECS.raw (small texture with pistol and shotgun muzzle flashes)



**How to generate Doom 64 disc image**

Somehow acquire a Doom 64 ROM in Z64 format.

Check that your Doom 64 ROM is the correct one.

The below is the expected md5sum output

    md5sum doom64.z64
    b67748b64a2cc7efd2f3ad4504561e0e doom64.z64

Now place a copy of `doom64.z64` in the `wadtool` directory.

Go to the repo directory and compile it like any other KallistiOS project. Make sure you source your KOS environment first. It is starting to look like there is an issue when it is built at -O2 without lto. Modify `environ.sh` so KOS_CFLAGS has `-O3 -flto=auto` instead of `-O2` before you build.

To build the source into an ELF file, run `make`.

    source /opt/toolchains/dc/kos/environ.sh
    cd ~/doom64-dc
    make clean
    make

As part of the build, `Make` will automatically build and run `wadtool`.

This should take a minute or less to run depending on your processor and disk speed.

The first terminal output you see should match the following except for the time values (the first time you run `make`):

	Script dir is: ~/doom64-dc/wadtool
    Compiling wadtool
    Running wadtool
    
    real   0m20.368s
    user   0m19.836s
    sys    0m0.233s
    Generated data files in specified selfboot directory: ~/doom64-dc/wadtool/../selfboot
    Done.

Subsequent runs will not rebuild `wadtool` but start at `Running wadtool` or skip that too if the generated files already exist in `selfboot`.

When it is complete, you will now have the following new files in the `~/doom64-dc/selfboot` directory:

    alt.wad
    pow2.wad
    tex/non_enemy.tex
	maps/MAP\*.wad

You now have all of the updated files required to run Doom 64 for Dreamcast in the places they need to be.

If you have `mkdcdisc` installed, you can use the `cdi` build target to create a self-booting CDI.

    cd ~/doom64-dc
    make cdi

If you have both `mksdiso` and `mkdcdisc` installed, you can use the `sdiso` build target to create an ISO suitable for loading from SD card with Dreamshell ISO loader.

    cd ~/doom64-dc
    make sdiso

If you are trying to use any other tool, you are on your own.

Good luck. :-)

# Acknowledgments

Immorpher [https://github.com/Immorpher] for introducing me to the entire Doom 64 RE scene, getting me hooked on hacking the N64 version

This port is heavily based off of his Merciless Edition and Doom 64 XE work [https://github.com/Immorpher/doom64xe]

which in turn would not be possible without the excellent work on reverse engineering Doom 64 by GEC, found here [https://github.com/Erick194/DOOM64-RE]


Everyone on the KOS team and the Simulant Discord but especially

Falco Girgis [https://github.com/gyrovorbis] and Paul Cercueil [https://github.com/pcercuei] for their tireless work on fixing and extending the KOS PowerVR code and answering *NON-STOP* questions about "IS THIS SUPPOSED TO WORK AND WHAT STUPID DID I DO TO MAKE IT NOT WORK"

Luke Benstead [https://github.com/kazade] for the excellent glDC project, where I learned what near-z clipping is and how to implement it. Completely lost without that.

"Lobotomy" for creating some really nice normal maps and helping me learn how to do the rest

"erik5249" for developing a BC5 compression scheme for normal maps that helped me achieve my goal of keeping all (compressed) textures cached in RAM; also, providing a reference compressor and very performant decompressor

Andy Barajas [https://github.com/andressbarajas] for help debugging my filesystem use around my normal map implementation, and his libwav for streaming ADPCM music (bundled in the repo)

Ruslan Rostovtsev [https://github.com/DC-SWAT] for pointing me toward streaming ADPCM instead of using ADX for soundtrack and his excellent KOS PRs for fixing the streaming audio performance.

Everyone who helped test builds along the way and offered endless moral support, thanks.
