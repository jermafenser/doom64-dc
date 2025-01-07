# Doom 64 for Dreamcast (updated 2025/01/07)

## Since apparently this is very difficult to understand, I'll write it simply: Doom 64 is still for sale and is active IP. 

![Bethesda](https://github.com/jnmartin84/doom64-dc/blob/main/images/support.png?raw=true)

## ANY redistribution of CDI/ISO files with game data is software copyright infringement. It doesn't matter if it is free or for money. It is against the law. Asking me about this makes you look bad, makes the Dreamcast scene look bad and is not a good look for me either. Stop playing dumb and language lawyering over it. You aren't getting support so stop asking.


So anyway, this is ultimate and probably final update to Doom 64 for Dreamcast.  Bug-fixes are still happening, so you may have your game freeze every so often.


Please pay close attention to the README as the build instructions have changed significantly.


- UNCAPPED FRAME RATE, variable with correct physics. 60 FPS in the majority of the game with full lights and normal mapping. 


- VMU SAVING IS NOW SUPPORTED. 5 free blocks are required.

Both settings and game passwords can be saved. Setting changes are saved automatically when you exit any settings related menu.

Game saves happen in the intermission screen after password is shown, or the 'Password' menu. I have only been able to test this with OEM Sega VMU. It is known that Performance 4x Memory Pack does NOT work.


- Rumble/Vibration/Purupuru Pack is also now supported. Go to `Options`, `Movement`, and select `Rumble: On.` I have only been able to test this with a RetroFighters StrikerDC wireless pad.


- Keyboard and mouse are also supported. I don't have much to comment on that, I got this from a pull request.


- Custom Knee Deep In The Dead content with Dreamcast-exclusive enhancements. Maps by z0k (with mods by Mittens). Music by Andrew Hulshult. 

Commercial redistribution of this additional content is not allowed. It is against the terms of use.

But because some of you are literal children, any redistribution of this project with game data is copyright infringement and prohibited by law.

- Anyway, speaking of the lights and normal mapping mentioned a few lines above...

There is full world and weapon real-time normal mapping with dynamic lighting, with world geometry tesselation (bUt DoT3 BuMp MaPpInG oN tHe DrEaMcAsT iS iMpoSsIbLe?!?! because...)

![fragment shaders](https://github.com/jnmartin84/doom64-dc/blob/main/images/fragment.png?raw=true)


**VRAM captures, running on Dreamcast hardware**

![Lost Levels](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_6373.png?raw=true)

![Altar of Pain](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_24836.png?raw=true)

![Even Simpler](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_18853.png?raw=true)

![Holding Area](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_1351.png?raw=true)

![Dark Citadel](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_30473.png?raw=true)

![Dark Citadel](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_1641.png?raw=true)

![Spawned Fear](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_34467.png?raw=true)

![Unholy Temple](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_4528.png?raw=true)

![Dark Citadel](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_4740.png?raw=true)

![The Spiral](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_4747.png?raw=true)

Lots of work went into this and I hope you all enjoy it.

You *will* have to do a tiny bit of actual work to get this going. If you don't have 30 to 45 minutes to spare, just go play on Steam and call it a day. The results are worth it though.


# playing guide

    A is attack
    B is use
    X is weapon backward
    Y is weapon forward
    X+Y brings up automap and cycles through (first press textured overhead map, second press line map, third press back to game)
    B+R automap zoom out
    B+L automap zoom in
    D-PAD/Analog Stick is move
    L trigger is strafe left (analog sensitive)
    R trigger strafe right (analog sensitive)
    START is start (bring up menu)


# build guide

**Pre-requisites**

Whatever the directory you cloned this github repo to is named and wherever it is located, it will be referred to in this document as

`doom64-dc`

This guide will assume that you cloned it into your home directory. 

If you need to get to the top level of the repo, it will say

    cd ~/doom64-dc

The build is known to work on the following platforms as of the current commit:

    Debian (version?)
    Ubuntu 22.04
    Windows 11 - Cygwin 64-bit
    Windows (version?) - DreamSDK

It should work on most other Linux environments.

You will need a host/native GCC install and a full working Dreamcast/KallistiOS compiler toolchain install.

See [https://dreamcast.wiki/Getting_Started_with_Dreamcast_development] for instructions.

A modified version of KOS is provided as part of the Doom 64 repo. This is the only version that will guarantee a working game. Please do not file github issues if you are not using it. They will be closed with prejudice.

These instructions assume it is the only version of KOS on your system. If you already have KOS installed, please move it elsewhere before you begin.

To set it up, after building/installing compilers, open a terminal and do the following (please pay attention to the `#` part):

    cd ~/doom64-dc
    tar xzf doom64_kos.tgz
    # WARNING: YOU NEED TO REPLACE any existing kos directory, please move it to a safe place first if KOS already exists
    # i.e.  mv /opt/toolchains/dc/kos ~/BACKUP_OF_MY_OLD_KOS
    cd ./doom64_kos
    cp -r kos /opt/toolchains/dc/
    cd ..
    rm -rf ./doom64_kos
    exit

Once you have the unpacked kos directory in place, open a new terminal.

Source the provided `environ.sh` file and build KOS as follows:

    cd /opt/toolchains/dc/kos
    source ./environ.sh
    make
    exit

Now you have a version of KOS identical to the version I use for development.

**Repo contents**

Whatever the directory you cloned this github repo to is named and wherever it is located, it will be referred to in this document as

`doom64-dc`

This guide will assume that you cloned it into your home directory. 

If you need to get to the top level of the repo, it will say

    cd ~/doom64-dc

Under doom64-dc, you will find

    doom64-dc/
    -- README.md (you're reading it right now)
    -- doom64_kos.tgz (modified KOS with new features and bugfixes)
    -- Makefile (how it gets built)
    -- doom64_hemigen/ (the tool I used to generate and compress all normal map textures)
    -- wadtool/ (the tool that builds texture and WAD files from Doom 64 ROM)
    -- selfboot/ (all files needed to make a bootable CD image)
    ---- bump.wad (BC5-compressed normal map textures in a WAD file)
    ---- symbols.raw (Dreamcast-specific SYMBOLS lump)
    ---- doom1mn.lmp (Dreamcast-specific KDITD sky lump)
    ---- maps/ (all game map WADs dumped from Doom 64 ROM by wadtool)
    ---- mus/ (all of the music tracks as 44khz stereo ADPCM)
    ------ mus*.adpcm (music tracks)
    ------ e1m*.adpcm (music tracks)
    ---- sfx/ (all of the game sfx as 22khz ADPCM WAV)
    ------ sfx_*.wav (sound effects)
    ---- tex/ (weapon bumpmaps and generated non-enemy sprite sheet)
    ------ bfgg_nrm.cmp (BC5-compressed BFG normal maps)
    ------ chgg_nrm.cmp (BC5-compressed chaingun normal maps)
    ------ lasr_nrm.cmp (BC5-compressed laser normal maps)
    ------ pisg_nrm.cmp (BC5-compressed pistol normal maps)
    ------ plas_nrm.cmp (BC5-compressed plasma rifle normal maps)
    ------ pung_nrm.cmp (BC5-compressed fist normal maps)
    ------ sawg_nrm.cmp (BC5-compressed chainsaw normal maps)
    ------ sht1_nrm.cmp (BC5-compressed shotgun normal maps)
    ------ sht2_nrm.cmp (BC5-compressed super shotgun normal maps)
    ------ lasr_nrm.cmp (BC5-compressed super shotgun normal maps)
    ------ wepn_decs.raw (small texture with pistol and shotgun muzzle flashes)

**How to generate Doom 64 disc image**

***N64 retail game support***

Somehow acquire a Doom 64 ROM in Z64 format and name it `doom64.z64` .

Check that your Doom 64 ROM is the correct one.

The below is the expected md5sum output

    md5sum doom64.z64
    b67748b64a2cc7efd2f3ad4504561e0e doom64.z64

Now place a copy of `doom64.z64` in the `wadtool` directory.

***Nightdive Lost Levels support***

Buy the Nightdive Studios edition of Doom 64 (the 2020 PC release) from Steam or wherever.

Check that your Doom 64 IWAD is the correct one.

The below are two possible md5sum outputs that will lead the the correct N64 format maps being generated.

    md5sum DOOM64.WAD
    654c57d19f5c4a52cf8c63e34caa2fd2 DOOM64.WAD

or

    md5sum DOOM64.WAD
    0aaba212339c72250f8a53a0a2b6189e DOOM64.WAD

Now place a copy of the Doom 64 IWAD from the installation directory in the `wadtool` directory, renamed to all lowercase `doom64.wad`.

***Compiling Doom 64 for Dreamcast***

Go to the repo directory and compile it like any other KallistiOS project. Make sure you source your KOS environment first.

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
    maps/map01.wad
    maps/...
    maps/map33.wad

If you provided a Nightdive Doom 64 IWAD you will also have the following additional files in `~/doom64-dc/selfboot`:

    maps/map34.wad
    maps/...
    maps/map40.wad

You now have all of the updated files required to run Doom 64 for Dreamcast in the places they need to be.

***Creating a disc image***

If you have `mkdcdisc` installed and reachable from your `PATH`, you can use the `cdi` build target to create a self-booting CDI.

    cd ~/doom64-dc
    make cdi

This will produce an output file `doom64.cdi` in `~/doom64-dc`.

If you have `mkisofs` installed and reachable from your `PATH`, you can use the `dsiso` build target to create an ISO suitable for loading from SD card or hard drive with Dreamshell ISO loader.

    cd ~/doom64-dc
    make dsiso

This will produce an output file `doom64.iso` in `~/doom64-dc`.

**Alternative instructions for disc image generation on Windows**

If you are on Windows and unable to build `mkdcdisc`, there is another way to make a self-booting CDI.

Grab the latest patched `BootDreams` from: https://github.com/TItanGuy99/BootDreams/releases

Follow the previous instructions located earlier in this document to generate an ELF file.

Next, from a `Cygwin`/`Mingw`/`DreamSDK` terminal, go to the repo directory and generate `1ST_READ.BIN` from the ELF:

    cd ~/doom64-dc
    sh-elf-objcopy.exe -O binary build/doom64.elf build/doom64.bin
    /opt/toolchains/dc/kos/utils/scramble/scramble.exe build/doom64.bin selfboot/1ST_READ.BIN

Start `BootDreams` and make sure it is on the `DiscJuggler` setting.

Check `ISO settings` (under the `Extras` menu).

Make sure to select `Full Filenames`, `Joliet` and `Rock Ridge`.

 ![BootDreams settings](https://github.com/jnmartin84/doom64-dc/blob/main/images/bd_settings.png?raw=true)

Now return to the main `BootDreams` window.

Click the `Browse` button next to the `Selfboot folder` section.

Find your `~/doom64-dc/selfboot` folder in the Windows Folder selection dialog. Single-click on it to select/highlight it and click OK.

Change the `CD label` if you feel like it.

You can leave `Disc format` on the default setting.

Click the `Process` button.

Click `Yes` when prompted if you want to create a DiscJuggler image.

If you get Error dialog about `missing IP.BIN`, click `Yes` to create one.

When the file dialog pops up, pick a location for your CDI file, change the name if you'd like and click `Save`.

Once the `CDI4DC` window disappears and you get the `Created successfully` dialog, click `OK`.

You're ready to go and can burn the image to CD or process it further.

If you are trying to use any other tool or operating system to make an image, you are on your own.

Good luck. :-)

**Notes on GDEMU usage**

I do not own a GDEMU so I cannot vouch for the correctness of the following section.

I was informed that some extra configuration is required to run the CDI from GDEMU. I was told the following settings added to `GDEMU.INI`will get Doom 64 to run. (edited because I am getting conflicting information)

    image_tests = 0


# Acknowledgments

Immorpher [https://github.com/Immorpher] for introducing me to the entire Doom 64 RE scene, getting me hooked on hacking the N64 version. His role as Subject Matter Expert was invaluable, helping me see (and hear) all of the inaccuracies and bugs I missed along the way.

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
