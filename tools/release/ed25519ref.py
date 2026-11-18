#!/usr/bin/env python3
"""Ed25519 as in RFC 8032, in the standard library alone.

FIRMWARE-MANIFEST.md section 3 says the algorithm is "Ed25519 as in RFC 8032 - no pre-hash, no
context". This is RFC 8032 section 6's own reference implementation, transcribed: the arithmetic
of section 5.1 over the curve's prime field, with SHA-512 from `hashlib`.

**Why not a library.** The release tooling of this repository signs a manifest in CI and writes
the fixtures the firmware's tests read. `cryptography` would do both, but it is a compiled wheel
on every runner that touches a release, and this repository pins every dependency it has to an
exact version with a reason (tasks/README.md). Nothing here is on a hot path - signing one 2 kB
manifest is milliseconds - and what guards it is the same thing that guards the firmware's own
verifier: **RFC 8032 section 7.1's first test vector**, which tools/release/test_release.py runs
over this file, and the specification's fixtures, whose signatures were made by the reference
implementation in another repository with `cryptography` and must verify here.

This is NOT what checks a release on a board. The firmware verifies with MeshCore's own
lib/ed25519 (C), which it already carries for its identity; src/update/Release.cpp is that side,
and test/test_release_reader proves it on the same test vector.

No constant-time claim is made or needed: this file handles a release signing key in CI, where the
only secret is the key itself and the only input is a manifest the same job just wrote.
"""

import hashlib

# RFC 8032 section 5.1: the curve edwards25519 over GF(2^255 - 19).
P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
I = pow(2, (P - 1) // 4, P)  # noqa: E741  sqrt(-1), the RFC's name for it


def _sha512(data: bytes) -> bytes:
    return hashlib.sha512(data).digest()


def _x_recover(y: int) -> int:
    xx = (y * y - 1) * pow(D * y * y + 1, P - 2, P)
    x = pow(xx, (P + 3) // 8, P)
    if (x * x - xx) % P != 0:
        x = (x * I) % P
    if x % 2 != 0:
        x = P - x
    return x


BY = 4 * pow(5, P - 2, P) % P
BX = _x_recover(BY)
B = (BX % P, BY % P, 1, BX * BY % P)
IDENTITY = (0, 1, 1, 0)


def _add(p, q):
    """Extended homogeneous coordinates (RFC 8032 section 5.1.4)."""
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    e = 2 * p[2] * q[2] % P
    f, g, h, j = b - a, e - c, e + c, b + a  # the RFC's E, F, G, H
    return (f * g % P, h * j % P, g * h % P, f * j % P)


def _mul(p, n):
    out = IDENTITY
    while n > 0:
        if n & 1:
            out = _add(out, p)
        p = _add(p, p)
        n >>= 1
    return out


def _equal(p, q):
    if (p[0] * q[2] - q[0] * p[2]) % P != 0:
        return False
    return (p[1] * q[2] - q[1] * p[2]) % P == 0


def _encode_point(p) -> bytes:
    zi = pow(p[2], P - 2, P)
    x, y = p[0] * zi % P, p[1] * zi % P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


def _decode_point(blob: bytes):
    if len(blob) != 32:
        raise ValueError("a point is 32 bytes")
    n = int.from_bytes(blob, "little")
    y = n & ((1 << 255) - 1)
    sign = n >> 255
    if y >= P:
        raise ValueError("the point's y is not in the field")
    x = _x_recover(y)
    if x == 0 and sign:
        raise ValueError("x is zero and the sign bit says otherwise")
    if x & 1 != sign:
        x = P - x
    point = (x, y, 1, x * y % P)
    # On the curve: -x^2 + y^2 = 1 + d x^2 y^2.
    if (-x * x + y * y - 1 - D * x * x * y * y) % P != 0:
        raise ValueError("the point is not on the curve")
    return point


def _secret_scalar(private: bytes):
    h = _sha512(private)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(private: bytes) -> bytes:
    """The 32 public bytes of a 32-byte private key (RFC 8032 section 5.1.5)."""
    if len(private) != 32:
        raise ValueError("an Ed25519 private key is 32 bytes")
    a, _ = _secret_scalar(private)
    return _encode_point(_mul(B, a))


def sign(private: bytes, message: bytes) -> bytes:
    """The 64 signature bytes (RFC 8032 section 5.1.6)."""
    if len(private) != 32:
        raise ValueError("an Ed25519 private key is 32 bytes")
    a, prefix = _secret_scalar(private)
    public = _encode_point(_mul(B, a))
    r = int.from_bytes(_sha512(prefix + message), "little") % L
    big_r = _encode_point(_mul(B, r))
    k = int.from_bytes(_sha512(big_r + public + message), "little") % L
    s = (r + k * a) % L
    return big_r + s.to_bytes(32, "little")


def verify(public: bytes, message: bytes, signature: bytes) -> bool:
    """RFC 8032 section 5.1.7, the whole of it: a malformed point, an S at or above L, or an
    equation that does not hold is False and never an exception."""
    if len(public) != 32 or len(signature) != 64:
        return False
    try:
        big_r = _decode_point(signature[:32])
        point_a = _decode_point(public)
    except ValueError:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    k = int.from_bytes(_sha512(signature[:32] + public + message), "little") % L
    return _equal(_mul(B, s), _add(big_r, _mul(point_a, k)))
