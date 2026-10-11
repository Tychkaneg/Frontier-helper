# Changelog

## 1.0.1 - 2026-10-11

- L3 turns once toward the nearest living, rendered large monster. It no
  longer recenters behind the hunter; no valid target leaves direction unchanged.
- START injects the targeting adapter alongside the matched native camera
  core, without installing or changing Rain Client files.
- Preserves the supplied FPS-sync native core, LT/L2 and RT/R2 handling,
  camera profiles, right-stick controls and collision behavior.
- Adds exact native-hash and monster-layout guards, adapter source, a
  reproducible string-only GUI patch, and isolated integration tests.
- Updates the displayed version and L3 help text to match the new behavior.
