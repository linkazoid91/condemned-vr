# Attribution and source lineage

## F.E.A.R. VR foundation

Condemned VR is derived from
[F.E.A.R. VR](https://github.com/DR-89/fear-vr), created by DR-89 and its
contributors and distributed under the MIT License.

The Condemned port began from upstream commit
[`24a6e22f20a02e64aa0955738f1050357b265400`](https://github.com/DR-89/fear-vr/commit/24a6e22f20a02e64aa0955738f1050357b265400)
(`beta.7`). The original upstream history is retained in this repository.

The reused foundation includes substantial portions of the:

- separate x64 OpenXR host and lifecycle;
- x86 D3D9 capture and D3D9Ex transport bridge;
- shared-memory protocol, ring buffers and heartbeat handling;
- stereo projection, head-pose and HUD math;
- OpenXR input and haptic transport;
- frame-pacing, diagnostics and structured logging code; and
- automated tests and build infrastructure subsequently adapted for
  Condemned.

Condemned-specific executable validation, loader behavior, engine hooks,
controller mappings, physical-melee work, scripts, tests and documentation
were added or reworked in this repository.

Some source-level identifiers still use `fearvr`, `FearVr` or `FEARVR` to
preserve protocol and implementation compatibility with the upstream code.
Those identifiers are attribution-bearing technical history, not evidence
that F.E.A.R. retail content is included or required.

The upstream copyright and MIT permission notice are preserved in
[`LICENSE`](LICENSE). Git history provides per-commit authorship details.

## Third-party projects

OpenXR-SDK, MinHook, DirectXMath and reference-only projects are documented in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). Their authors retain their
respective copyrights.

## Games and trademarks

This repository contains no *Condemned: Criminal Origins* or *F.E.A.R.* game
content. Those games, their assets, names and trademarks belong to their
respective owners. Condemned VR is an unofficial fan project and is not
affiliated with or endorsed by the game publishers, developers or trademark
owners.
