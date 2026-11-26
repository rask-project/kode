<p align="center"><img src="assets/kode.png" width="160" alt="Kode"></p>

# Kode

A native macOS text and code editor built with Qt Quick. *Kode* is
Norwegian for "code".

Part of the [rask-project](https://github.com/rask-project) family, with
Konsoll and Metronom, built on their shared
[Rask](https://github.com/rask-project/rask) modules. See
[docs/ROADMAP.md](docs/ROADMAP.md) for the plan.

## Building

Requires macOS 14.4+ (Apple Silicon), Qt 6.12, CMake 3.21+ and Ninja.

```sh
git clone --recurse-submodules https://github.com/rask-project/kode.git   # or: git submodule update --init
cmake -B build/Qt_6_12_0_for_macOS_Debug -G Ninja \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/macos -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Qt_6_12_0_for_macOS_Debug
open build/Qt_6_12_0_for_macOS_Debug/Kode.app
```

## License

MIT — see [LICENSE](LICENSE).
