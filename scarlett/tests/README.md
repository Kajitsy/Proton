# Proton-Scarlett Verification & Smoke Tests (`scarlett/tests/`)

## Phase 0 Smoke Suite

- `test_phase0_scaffold.cpp` — C++20 compilation, linkage, and repository integration check across all 7 Scarlett subsystems (`xdl`, `loader`, `xmem`, `dstorage_xs`, `xg_xs`, `xgameruntime`, `audio_media`), `Makefile.in`, `proton`, and `default_pfx.py`.
- `test_proton_scarlett_env.py` — Unit test suite verifying `MicrosoftGame.Config` XML detection (`Scarlett` / `XboxGen9`), PE64 binary header validation, non-interference with standard PC Proton launches, `WINEDLLOVERRIDES` (`=b`) generation for all 18 console DLLs, CA certificate forwarding in `passthrough`/`lan` network modes, and `default_pfx.py` symlink resolution.
