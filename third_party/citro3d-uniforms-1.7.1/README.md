# Citro3D 1.7.1 emission references

Unmodified primary sources from https://github.com/devkitPro/citro3d/tree/v1.7.1/source,
with the upstream zlib notice in LICENSE. Their SHA-256 values are pinned by
`tests/native_renderer/prepare_uniform_oracle.py`. They compile as independent
host oracles for the native renderer's register and uniform tests; they are
not linked into the ARM game.

The adapted native uniform implementation
(`source/native_renderer/native_uniforms.cpp`) retains the full notice in its
own source. The native indexed draw packet keeps the exact `drawElements`
register sequence and is checked against all 30 upstream GPU words over
100,000 ranges.
