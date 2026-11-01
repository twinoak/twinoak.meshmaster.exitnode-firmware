# MeshCore's private libraries, and the host build's SHA-256, for a consumer of the package.
#
# MeshCore keeps its ed25519 implementation in its own lib/ed25519 - a PlatformIO *private*
# library of its project, which is exactly what a project that depends on MeshCore as a package
# does NOT get: lib_extra_dirs pointed at it does not reach the dependency's own sources either,
# because a library with a manifest resolves its dependencies from that manifest and ed25519 is
# not in it. So the sources are added to the build here, from the pinned checkout and never
# copied into this repository, and the include path is in platformio.ini beside the variant's.
#
# A pin bump that moves or renames lib/ed25519 fails here, loudly, which is the
# point - docs/upstream.md lists this file among the places a bump re-reads.
#
# And, since task p1.8-32, three files of rweather/Crypto for [env:native] alone: SHA256.cpp,
# Hash.cpp and Crypto.cpp, which are the SHA-256 and HMAC-SHA256 a flood scope's transport key and
# transport code are made of (src/mesh/TransportScope.cpp). The board environments get the whole
# library from lib_deps as before; the native one cannot, because the library's RNG.cpp includes
# <Arduino.h> and a host has none - so [env:native] lib_ignores Crypto and these three
# translation units, which need nothing but <inttypes.h>, <stddef.h> and <string.h>, are added
# here instead. A release of the library that moves or renames one of them fails here, loudly.

import os

Import("env")  # noqa: F821  (SCons injects it)

libdeps = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))
meshcore = os.path.join(libdeps, "MeshCore")
ed25519 = os.path.join(meshcore, "lib", "ed25519")

if not os.path.isdir(ed25519):
    raise Exception(
        "MeshCore's lib/ed25519 is not at %s - has the pin moved it? (docs/upstream.md)" % ed25519
    )

env.BuildSources(os.path.join("$BUILD_DIR", "meshcore_ed25519"), ed25519)

if env.subst("$PIOENV") == "native":
    crypto = os.path.join(libdeps, "Crypto")
    wanted = ["SHA256.cpp", "Hash.cpp", "Crypto.cpp"]
    missing = [f for f in wanted if not os.path.isfile(os.path.join(crypto, f))]
    if missing:
        raise Exception(
            "rweather/Crypto's %s is not in %s - has the pinned release moved it? "
            "(docs/upstream.md)" % (", ".join(missing), crypto)
        )
    env.BuildSources(
        os.path.join("$BUILD_DIR", "crypto_sha256"),
        crypto,
        src_filter=["-<*>"] + ["+<%s>" % f for f in wanted],
    )
