# IW6x/iw6-mod: A Modded Client

This is a client modification for IW6!  
Developed by X Labs and [AlterWare](https://alterware.dev).  
This is a fork of IW6x/[iw6-mod](https://git.alterware.dev/alterware/iw6-mod) with added CB patches.  
Big thanks to all the contributors.              
Join us on [Discord](https://cbservers.xyz/discord) for support.  
Follow the original project on [GitHub](https://github.com/alterware) or [Twitter](https://x.com/alterwaredev).

NOTE: This fork is not affiliated or endorsed by AlterWare. Please do not bug original client maintainers with support requests in regards to this fork.

## Changes
- Remove branding by default (`ui_showBranding`)
- Add master server IP/Port dvar (`masterServerIP` & `masterServerPort`)
- Add custom texture support with "images" folder (from OG IW6x)
- Send in-game chat as "say/say_team" GSC notifies
- Fix bug with say commands not always registering
- Add GSC methods for IO
- Fix killcam slowdown (thanks to Joelrau h1 fix)
- Show proper errors to user when failing to connect to servers
- Add OG MW2 mechanics with `pm_improvedMechanics` dvar (thanks to plugwalker47)
- Add map related commands to client as well
- Use shared CB GUID keys

## Build
- Install [Visual Studio 2022][vs-link] and enable `Desktop development with C++`
- Install [Premake5][premake5-link] and add it to your system PATH
- Clone this repository using [Git][git-link]
- Update the submodules using ``git submodule update --init --recursive``
- Run Premake with the option ``premake5 vs2022`` (Visual Studio 2022). No other build systems are supported.
- Build project via solution file in `build\iw6-mod.sln`.

Only the x64 platform is supported. Do not attempt to build for Windows ARM 64 or Win32.

### Premake arguments

| Argument                    | Description                                    |
|:----------------------------|:-----------------------------------------------|
| `--copy-to=PATH`            | Optional, copy the EXE to a custom folder after build, define the path here if wanted. |
| `--dev-build`               | Enable development builds of the client. |


## Disclaimer

This software has been created purely for the purposes of
academic research. It is not intended to be used to attack
other systems. Project maintainers are not responsible or
liable for misuse of the software. Use responsibly.

[premake5-link]:          https://premake.github.io
[git-link]:               https://git-scm.com
[vs-link]:                https://visualstudio.microsoft.com/vs