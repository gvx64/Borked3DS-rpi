GVX64 - Borked3DS-rpi

This is a fork of rtiangha's Borked3DS which patches the opengles-dev-v2 branch
for better functionality on the Raspberry Pi 4 & 5.  Note that this build
requires the use of the OpenGL Graphics API (with GLES extensions enabled
in the configuration menu). The vulkan renderer on this build is likely not 
functional.

Instructions for Building from Source:

1. cd /home/pi

3. sudo git clone https://github.com/gvx64/Borked3DS-rpi.git

3. cd ./Borked3DS-rpi

4. sudo git submodule update --init --recursive

5. sudo mkdir ./Build ; cd ./Build

6. sudo cmake .. (note minimum cmake 3.12 requirement)

7. sudo make -j4

<h1 align="center">
  <br>
  <a href="[https://github.com/Borked3DS]"><img src="https://github.com/user-attachments/assets/7fd0ed50-1e1f-4b0a-ba31-a524737705c5" alt="Borked3DS" width="200"></a>

  <br>
  <b>Borked3DS</b>
  <br>
</h1>


<b>Borked3DS</b> is yet another Citra-derived project that aims to continue Nintendo 3DS emulation development work after that project shut down. Its lineage is directly derived from [PabloMK7's now archived fork](https://github.com/PabloMK7/citra), but also incorporates elements from [Lime3DS](https://github.com/Lime3DS/Lime3DS) (also now defunct) and [Mandarine](https://github.com/mandarine3ds/mandarine), as well as providing its own stuff.

<p align="center">
<img src="https://github.com/user-attachments/assets/bf14a1cb-5709-43b9-9cb8-5d953e266d8f" alt="Borked3DS Configure Graphics">
</p>

# About Borked3DS
This project is basically a rebranding and continuation of **[Bravely Offline Citra](https://github.com/Borked3DS/Borked3DS/tree/bravely-offline-citra)**, which aimed to be a drop-in replacement for the bundled version of the QT5-based Citra r1800 that is included with [Bravely Offline](https://github.com/osm70/bravely-offline), but where development continued past the point where it really should have become its own thing much sooner.

It's more experimental than the other forks, so consider Borked3DS as more of a _Canary_ project (or a YOLO project, if we're being honest; or a Sandbox project if we're being kind) with limited testing and minimal support (meaning there may be bugs that may not be discovered or fixed for a while due to this being mainly a hobby project on behalf of the main developer, so just assume that things can break at any time).

Thus, if you want a more stable or reliable experience, you are encouraged to use one of the other forks instead.

**NOTE**: This is **NOT** [the official project](https://github.com/azahar-emu/azahar) that aims to unify and continue Lime3DS and PabloMK7 development; while elements from here may make their way to that new project when it comes online, for the moment, development here has diverged far enough from what PabloMK7 and Lime3DS were doing such that it was felt that it should become its own thing, at least until the new unified project comes online.

<img src="https://github.com/user-attachments/assets/52481299-012c-4c04-a46c-7f98e025e245" alt="Borked3DS Home Screen Android"> <img src="https://github.com/user-attachments/assets/ac1bf2f0-b157-4838-a8d2-c30e4d6f4bd8" alt="Borked3DS General Settings Android"> <img src="https://github.com/user-attachments/assets/beab1dae-05bc-4ebf-9f30-25641dd4a3a7" alt="Borked3DS Graphics Settings Android">

<img src="https://github.com/user-attachments/assets/cfebc598-7daa-4c3d-95e5-f7f02687a5f2" alt="Borked3DS Audio Settings Android"> <img src="https://github.com/user-attachments/assets/1a347399-e6d8-4f3b-b0f7-1c7c7df4d331" alt="Borked3DS Layout Settings Android"> <img src="https://github.com/user-attachments/assets/84c28de6-32f1-4b2b-bc78-5cec31b92451" alt="Borked3DS Debug Settings Android">

<img src="https://github.com/user-attachments/assets/d8226af9-9b1d-4b3c-a610-c4263cee0671" alt="Borked3DS Portrait Layout Black Android"> <img src="https://github.com/user-attachments/assets/8e038f6e-c539-42fa-9c98-3fc3bdb5a646" alt="Borked3DS Portrait Layout Red Android"> <img src="https://github.com/user-attachments/assets/71a1c33c-71a8-4f95-82a9-b1bebab2e85c" alt="Borked3DS Portrait Layout Blue Android">

# Differences between Borked3DS and the other forks:

This project has everything that PabloMK7's fork had before it shut down, most of Lime3DS's major features before it shut down, some of Mandarine's hacks and features, and some features that were in development or considered pre-release that were never merged into the various forks before they shut down.

### In addition, some of the features exclusive to this fork (as of [PabloMK7 r608383e](https://github.com/PabloMK7/citra/releases/tag/r608383e), [Lime3DS 2119](https://github.com/Lime3DS/Lime3DS/releases/tag/2119), and [Mandarine 1.1](https://github.com/mandarine3ds/mandarine/releases/tag/r1.1)) are:

* **Near-feature parity in terms of settings and functions between the Desktop and Android versions**: Virtually all of the options in the Desktop version that are applicable to the Android version are here, except for one (Offset Time, for the record). Whether or not these features actually _work_ on your device may be dependent on your device's GPU or driver support situation though (or some may have been buggy to begin with; in which case, PRs with fixes welcome). For some features, you may have better results with hardware that supports Vulkan 1.3+ and/or OpenGL 4.6+.
* **Built-in Skylanders IR portal support on Desktop**: If you possess that hardware, it should work natively on Borked3DS in most of the 3DS games that support the hardware (the management interface can be found in the `Tools` menu, and more details in general can be found from the original dev [here](https://www.reddit.com/r/skylanders/comments/1cu8bsa/emulated_3ds_portal_on_citra/)).
* **The ability to customize SPIR-V shader optimization in Vulkan for Desktop and Android**: Previously, things were hardcoded to optimize only for size, and only through `Glslang` with no option to disable or change it. This project pipes those shaders directly through `spirv-opt` instead and further allows you to choose to optimize for size, performance, or to disable it entirely and revert to the legacy method. You can also choose to activate SPIR-V validation and/or legalization, which may help with debugging and testing custom shaders.
* **Better logging options on Android**: You can now choose verbosity by log level, and the regex filtering feature found in PabloMK7's fork is here too. By default, Android logging was hardcoded at the `:Info` level, which meant that you were missing some messages that you may have been entitled to or that can help other devs in troubleshooting.
* **Better tooltips and descriptions for all options in all frontends**: If you don't know what a setting does, there's probably some help text for it in this project. More important these days since the main Citra wiki was lost and documentation is scattered or lost. Implemented because I'm still new to the Citra world and I don't know what everything does, the option names may not be intuitive (to me, at least), and I'm too lazy to ask ChatGPT more than once. While I totally respect those who prefer more minimal interfaces, I think having local help available on demand without having to launch a web browser is more user friendly (That said, I don't have a Transifex account (yet), so unfortunately the non-English translations haven't kept up.).
* **A more compact and streamlined Desktop interface** (i.e. No more having to resize the settings window because there are too many options to fit on the screen): Useful for people like me who still have to work with lower resolution displays on their laptops. My biggest beef with the previous forks was that there was a lot of wasted space in the Settings UI because things were rendered vertically. So now, things go horizontal as well, which is a more efficient use of screen space (see Screenshots above).
* **More choices in terms of versions to deploy**: There are Linux AppImages based off of Ubuntu 20.04, 22.04 and 24.04 (rather than just 22.04), and versions that are compiled with GCC and Clang for Windows and Linux. You can choose whichever one performs best for you on whatever hardware/software you own.
* **Third party dependencies are more recent than the other forks** (ex. Boost 1.85.0, LibreSSL 3.9.2, FFmpeg n7.1 support, etc. are just some of the dependencies not yet implemented by the other forks), and because they are built from Git source (even if a binary version is available from upstream), are sometimes bleeding edge (ex. If there are interesting features or bug fixes introduced in between dependency versions, they may appear here before the upstream projects release their next official update). So think bleeding edge input driver support through SDL as soon as they're available, support for the various Vulkan API updates inbetween major Vulkan SDK version releases, or binaries created with the latest compilers available for any supported platforms.
* **And much, much more** (especially on the backend and where it comes to the build system, for example using GitHub runners rather than Docker images to build Linux AppImages or using GPUCode's [x86-optimized version](https://github.com/raphaelthegreat/teakra) of Teakra for x86 builds while the other forks do not).

# System Requirements
Note that Borked3DS performance is highly dependedent on GPU hardware and driver support, although software rendering may also offer a positive experience if your CPU has  fast enough single-threaded performance.

### Desktop (Windows, MacOS, Linux)
```
Operating System: Windows 10 (64-bit), MacOS 13 Ventura for x86, MacOS 14 Sonoma for ARM, or modern 64-bit Linux
CPU: x86-64 (64-bit) CPU. Single core performance higher than 1,800 on Passmark (Recommended: The faster, the better)
GPU: OpenGL 4.3 or Vulkan 1.1 support (Recommended: OpenGL 4.6+ or Vulkan 1.3+ support)
Memory: 4GB of RAM (Recommended: 8GB+ for custom textures, 12GB+ if preloading)
```
### Android
```
Operating System: 64-bit Android 9.0+ (ARM or x86)
CPU: Snapdragon 835 SoC or better
GPU: OpenGL ES 3.2 or Vulkan 1.1 support (Recommended: Vulkan 1.3+ support)
Memory: 4GB of RAM (Recommended: 8GB+ for custom textures, 12GB+ if preloading)
```

# Installation
Download the latest release from [Releases](https://github.com/Borked3DS/Borked3DS/releases).

In the Desktop Release packages, two main executables are provided:

* `borked3ds`: The QT-based Desktop GUI frontend (Most users will want to use this one)
* `borked3ds-cli`: The SDL-based command line frontend (Useful for scripting or for use with other frontends)

Both versions can take command line options. Invoke with `--help` to see what's available.

#### Windows Version Differences
There are three different versions offered for 64-bit Windows users, created using different compilers (MSVC, Clang, and GCC). 

How they differ:
* MSVC generates a smaller executable compared to GCC and Clang.
* Microsoft developed the MSVC compiler (which is closed source) while Clang and GCC are open-source.
* The MSVC version requires the installation of [Microsoft Visual C++ runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist), if not already installed, which can require a restart to finish the installation. If you have issues with the Microsoft Visual C++ runtime, you should try the Clang or GCC builds instead.
* There have been reports where games under MSVC do not work while working under Clang or GCC, and vice versa.

Try them all and use whichever version performs best for you. Some may work better for some games than others.

### Linux Version Differences

A variety of different 64-bit Linux AppImages are offered based off of Ubuntu 20.04, 22.04 and 24.04. Most AppImages are compiled with Clang 19, although a GCC 14 option based off of Ubuntu 24.04 is also made available. Use whichever version works best on your hardware and OS distribution combination.

### MacOS

The provided unified binary will run on both 64-bit x86 and ARM based Macs. MacOS 13 is the minimum version for x86 based Macs, while MacOS 14 is the minimum version for ARM based Macs.

### Android

The provided apk will install on 64-bit Android 9+ devices running on ARM or x86 hardware. The best experience can be found on hardware devices supporting Vulkan 1.3+.

# Build Instructions

COMING SOON (In the meantime, look at the `.ci` directory for compile options that can be gleaned from the various build scripts and `.github/workflows/build.yml` for hints on what system packages to install before compiling, or just fork the directory, enable GitHub Actions, and use GitHub's infrastructure to automatically build yourself a copy).

# Contributing

[Pull Requests](https://github.com/Borked3DS/Borked3DS/pulls) with fixes or improvements are always welcome.

However, please [enable GitHub Action workflows](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/enabling-features-for-your-repository/managing-github-actions-settings-for-a-repository) on your fork and ensure that your changes pass all build tests first before submitting a PR. Sometimes things built locally won't compile on another OS or compiler without some minor modifications; using the included build tests invoked through GitHub Actions whenever you commit a change can help detect some of those issues and offer hints on how to fix them. This helps to reduce the support burden for everyone and increases the likelihood that your PR will be accepted here or elsewhere.

# FAQ

### Why did you name it Borked3DS?
I wanted a name that reflected its experimental nature and (most likely) minimal support, and unfortunately, Broken3DS was already taken and I couldn't find a citrus-based fruit name that I felt jived with the project's goals.

### What happened to Bravely Offline Citra?
Bravely Offline Citra was originally a project that aimed to be used as a drop in replacement for the version of Citra r1800 bundled with Bravely Offline. The understanding at the time was that modern Citra didn't work with the Bravely Offline server, so the project aimed to at least update its third party dependencies and backport some other features from modern QT6-based Citra to the QT5-based r1800 build.

However, it turned out that the modern forks did work with Bravely Offline with no modifications needed, making a project based on the original premise moot. Thus, at that point, the project essentially became a hobby project looking to see how far the dependencies could be pushed before things broke, and to play around with developing and testing some new features before submitting PRs to PabloMK7's fork and Lime3DS where better code scrutiny and testing could occur due to the larger user bases.

Now with both PabloMK7's fork and Lime3DS shut down in favor of a new unified project with an unknown timeline as to when it will go live and start accepting contributions again, and considering that lots of changes were made here in terms of the build system, feature set, and implementation of certain features that may or may not make it difficult to port everything to the new project when it's ready, it was felt that more of a hard fork could be justified to emphasize the differences and distinguish this project from the others (and to help continue development while waiting for the unified project to come online).

That said, this fork will still work with Bravely Offline. Just rename the `borked3ds.exe` executable to `citra-qt.exe` and copy/paste the entire bundle into the relevant Citra directory in Bravely Offline like before and run the client/server program normally. Alternatively, you can find the last version of Bravely Offline Citra [here](https://github.com/Borked3DS/Borked3DS/releases/tag/v2024.10.29a).

### So what happens to Borked3DS when that new unified project comes online?
Certainly, the work to port any relevant and useful features from here to the new project will happen when that project is ready. If everything that's different from here manages to make its way over there, then the need for this project will be re-evaluated. But with no timeline for that project to become live yet specified and while development is still fun for me, Borked3DS will continue for the near future.

### What about a Discord server, Flatpak support, subreddit, etc.?
I'm not a big Discord or Reddit user, don't really use Flatpaks, and this was really just a hobby project where I could share my work whenever there was something useful developed, and to submit a PR to other forks whenever I felt something was mature and stable enough. Because my volunteer hours are limited, I can't dedicate much more time to community management, volunteer management, and working with environments outside of what GitHub offers by default on top of what I'm already spending; I already spend too much time on development work and not enough time playing games. I feel like what is already offered here is enough to get people going, and at least until recently, the other forks provided some of those other things that are missing here. But if there's enough demand, perhaps some of those requests may be taken under consideration. But I make no promises because I'm not sure how long this project will stick around for, especially once that new unified project comes online.

### I hate the name.
It's part ironic, part truthful, and it makes me laugh. If this project ends up being successful and long lasting, it will be because it successfully lowered everyone's expectations from the beginning, and so it stays.
