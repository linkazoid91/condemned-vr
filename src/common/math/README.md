# Shared math

Coordinate conversion, pose composition, stereo projection and weapon
kinematics live in small header-only units under `src/common/`. They compile
for both the x86 game-side modules and x64 OpenXR host and must not depend on
engine-owned pointers or an active headset.

Every conversion has one authoritative implementation and corresponding
headset-free tests. See `docs/COORDINATE-SYSTEM.md`.
