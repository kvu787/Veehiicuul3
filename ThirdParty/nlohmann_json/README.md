# nlohmann/json

Vendored **3.12.0**, with the upstream single header unchanged. The MIT license
wording is unchanged; trailing whitespace is removed to match `.editorconfig`.
Builds use this checked-in dependency and require no network access or package manager.

- [Release and published checksum](https://github.com/nlohmann/json/releases/tag/v3.12.0)
- [Header source](https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp)
- [License source](https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT)
- Header SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`

The local CMake interface target is `nlohmann_json::nlohmann_json`. It disables
implicit conversions; declarative macros perform typed field conversion.
The application I/O adapter supplies a local serializer policy that requires
integer tokens and checks representability before integer conversion. The
vendored library remains unchanged.
Only the settings I/O library and deserialization tests need this dependency.
The settings model, validation, renderer preparation, and reusable SimplePaint
module are independent of JSON.

To update, replace the header and license from a pinned upstream release, verify
its published header checksum, update this file, and run both Release and Debug
tests through `Run.ps1`.
