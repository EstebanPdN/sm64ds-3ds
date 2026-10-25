# Remaining work

BUILD 24 addresses the identified first-boot settings crash and makes local
candidate identification visible. The next work is acceptance against the
same executable and evidence-driven fixes, rather than assuming a build
proves complete playability.

## Before a public test package

- Qualify installation, first boot and normal title/file progression on
  identified physical Old and New 3DS systems.
- Verify save/reopen, existing saves, backup recovery, sleep/resume, HOME and
  exit behavior. Exercise full SD and failed-write paths; show save failures
  clearly to the user where callers still only log them.
- Check audible output and transitions on hardware, including previously
  reported crackles. Queue/gain counters are evidence, not proof of a cure.
- Review wide/original display modes, top/lower HUD, stereo toggles, camera
  directions and pause/options states across representative levels.
- Exercise complex movement handoffs: water, damage, transformations,
  abilities, scripted transitions, moving platforms and actor lifetime.
- Run a documented Old/New performance matrix with camera sweeps, crowded
  scenes and loading. Attribute long frames to measured subsystems before
  removing work or changing synchronization.
- Regenerate, audit and package a closed candidate from the final source;
  keep its ELF, source receipt, visible BUILD and package hashes together.

## Longer coverage

Full-game progression, additional courses/characters, minigames and prolonged
sessions still need systematic testing. Region expansion requires explicit
data/schema work; another ROM's filename is not compatibility. Source and
documentation cleanup does not establish these outcomes.

Dated investigations and detailed open lists remain in
[the documentation index](README.md). Confirm their candidate scope before
turning an old unchecked item into a new task.
