# IW6: A Modded Client

This is a client modification for IW6!
**NOTE**: You must legally own Call of Duty®: Ghosts® to run this mod. Cracked/Pirated versions of the game are **NOT** supported.

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

## Contributing

Contributions are welcome! Please follow the guidelines below:

- Sign [AlterWare CLA][cla-link] and send a pull request or email your patch at patches@alterware.dev
- Make sure that PRs have only one commit, and deal with one issue only

## Disclaimer

This software has been created purely for the purposes of
academic research. It is not intended to be used to attack
other systems. Project maintainers are not responsible or
liable for misuse of the software. Use responsibly.

[premake5-link]:          https://premake.github.io
[git-link]:               https://git-scm.com
[vs-link]:                https://visualstudio.microsoft.com/vs
[cla-link]:               https://alterware.dev/cla
