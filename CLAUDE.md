# IKriate

## Versioning
- The release number is in `IKRIATE_VERSION` (source root). CMake, the Android
  Gradle build and CI read it, so package names (`ikriate-arm64-v8a-X.Y.Z…apk`,
  `ikriate-x64-X.Y.Z….zip`, `IKriate-X.Y.Z-x86_64.AppImage`) follow it.
- Bump the minor number (1.3.0 → 1.4.0 → …) in the commit of every new set of
  changes the user asks for; a set that is only a fix to the previous set's build
  stays on the same number.
