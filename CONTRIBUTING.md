# Contributing

Use the versions in `toolchain.env`, keep dependency lockfiles updated, and run
the narrow component check before `make check`. Generated app protocol and
localization files must match their JSON sources. Schema changes require a
committed migration and PostgreSQL tests. Do not commit secrets or generated
build output.
