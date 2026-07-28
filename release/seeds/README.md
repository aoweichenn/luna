# Bootstrap seed identities

Each file in this directory fixes the canonical SHA-256 identity of one
released Luna bootstrap seed. The build target regenerates the archive and
requires its checksum file to match the tracked value byte-for-byte.

A checksum authenticates nothing by itself. Consumers must obtain this
directory from the authenticated source tag corresponding to the seed
version. Once that tag is released, its checksum is immutable.
