Windows-specific dynamic link library dependencies.

The Windows workflows copy everything in this folder into the final `release-64/libs`
output. If required runtime DLLs are missing here, Windows builds will succeed but the
published artifact will launch with missing-library errors. Make sure the proprietary
or prebuilt DLLs the application depends on (e.g., bundled plugin runtimes) are added
before cutting a release.
