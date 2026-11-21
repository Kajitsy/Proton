# Loader & VFS (`scarlett/loader/`)

PE64 import resolver, `MicrosoftGame.Config` manifest loader, `GAME:` / `T:` / `S:` CoW overlay VFS, and unencrypted XVD/XVC package inspect tools.

## Clean-Room & Security Invariants

- Rejects encrypted XVC/XVD volumes (`KeyId != 0` or encrypted flags set) immediately with `SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED`.
- Enforces builtin DLL resolution for all 18 console modules to prevent `STATUS_ENTRYPOINT_NOT_FOUND` (`0xC0000139`).
