# IW6x/iw6-mod: A Modded Client

This is a client modification for IW6!

Developed by [AlterWare][aw-link] and X-Labs.

This is a fork of [iw6-mod][iw6-git-link] with CB patches.

Big thanks to all the past contributors and supporters.
                        
Join us on [Discord][discord-link] for support.

Follow the original project on [GitHub][aw-git-link] or [Twitter][aw-twitter-link].

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

[aw-link]:                https://alterware.dev
[aw-git-link]:            https://github.com/alterware
[aw-twitter-link]:        https://x.com/alterwaredev
[iw6-git-link]:           https://git.alterware.dev/alterware/iw6-mod
[discord-link]:           https://cbservers.xyz/discord
[premake5-link]:          https://premake.github.io
[git-link]:               https://git-scm.com
[vs-link]:                https://visualstudio.microsoft.com/vs