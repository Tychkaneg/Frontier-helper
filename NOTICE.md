# Rights and source provenance

## Original project material

Copyright (c) 2026 Tychkaneg.

The MIT grant in [LICENSE](LICENSE) covers original Frontier Helper material
only to the extent the project's authors hold the rights needed to grant it.
It does not transfer or grant rights in third-party material or the underlying
game. Third-party rights and applicable terms remain with their holders.

## Monster Hunter Frontier and Capcom

Monster Hunter Frontier game code, assets and associated names/trademarks
remain the property of Capcom and their respective rights holders. Frontier
Helper is an independent fan-made mod; this repository does not claim Capcom
affiliation, endorsement or approval.

A file-list review on 2026-10-11 found no complete game client, game texture
archives or music in the tracked repository or the packaged Helper ZIPs.
The runtime files in those packages are Helper executables and mod DLLs.
This is a bounded inventory check, not proof that every byte is independently
authored or cleared for redistribution.

Engine compatibility headers include short instruction-byte sequences
obtained from inspection of the running game. They serve as compatibility
signatures. The mod also uses engine addresses and data-layout information.
This repository does not claim that Capcom has licensed those instruction
sequences under MIT. Whether their use is permitted requires consideration
of applicable law and relevant terms; this notice does not decide that issue.

## Supplied FPS-sync native binary

The updated source corresponding to the supplied FPS-sync
`app/FrontierOrbitRain08.dll` was not available for the 1.0.1 release. The
repository preserves that binary and records its SHA256 and technical tests
in [the verification report](app/verification/monster-target-1.0.1.json).
Those checks establish the tested file and behavior; they are not a complete
audit of its authorship, incorporated material or redistribution rights.
The older source snapshot does not reproduce that updated binary.

## Limits of the license notice

An MIT license for the project's own work does not grant permission to
modify, reverse-engineer or redistribute Capcom's game, or override relevant
third-party terms. This file records licensing scope and known provenance;
it does not state that the mod has been legally cleared or approved by Capcom.
