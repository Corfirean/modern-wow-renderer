# Bundled Ascension area database

`areas.txt` was exported from the running Ascension client and verified against
its `patch-M.MPQ` tables (2849 areas, 374 maps). The executable SHA-256 is
`e7c2a69cb86804eb9e21254b7b45c6a03e532d8b8f94451d9e0855f7535f97c6`.
The AreaTable/Map structure and IDs matched; Area 1's live name is Dun Morogh.
See `docs/ENVIRONMENT-SYSTEM-RU.md` for the original verification record.

Rechecked offline on 2026-10-03: executable fingerprint and location getter/setter
instructions still match. This is the installed client version, not a claim
that every launcher channel uses the same executable or tables.

Unknown executables do not opt into the bundled offsets. New builds require
independent location and table verification before adding support. Custom
client installations can export and supply their own authoritative database.
