# Environment database builder

Uses the installed client's DBC files; no bundled vanilla zone list. Python 3.11+
and `mpyq` are required. Offline executable inspection additionally uses
`pefile` and `capstone`. The renderer itself needs none of these packages.

From the repository root:

```powershell
python tools/EnvironmentDatabaseBuilder/build_database.py C:\games\Ascension
```

The first run inventories all `.mpq` files under `Data`, including locale and
custom directories. It looks up `DBFilesClient\AreaTable.dbc` and `Map.dbc`
directly in hash tables, so archive listfiles are not required. It records
payload hashes and read/layout errors in `data/areas.inventory.json`.

It does **not** assume alphabetical filename order is Ascension's load order.
If all readable copies are identical and no archives failed, it can safely
select that payload. Conflicting copies or unreadable/custom formats require
one of these explicit sources:

```powershell
# Files exported through the actual client's virtual filesystem:
python tools/EnvironmentDatabaseBuilder/build_database.py C:\games\Ascension --dbc-dir C:\exports\DBFilesClient

# A JSON array of ACTIVE archive paths relative to the client, low priority
# first and highest priority last, verified against the actual client loader:
python tools/EnvironmentDatabaseBuilder/build_database.py C:\games\Ascension --archive-order C:\exports\active-mpq-order.json
```

Unsupported/encrypted tables in the selected source fail extraction; no stock
DBC is silently substituted. Supported layouts are WDBC AreaTable (36 fields)
and Map (66 fields), with sixteen localized name slots. Some old archives in
this installation contain a different Map layout (126 fields); it is reported
as unsupported. Deleted files/custom loader transformations are not emulated:
use a client VFS export for those cases. Verify the active archive manifest
before calling it authoritative.

Output is `data/areas.txt`, a versioned UTF-8 human-readable database. Records:

```text
EnvironmentDatabase 1
# provenance=explicit-client-vfs-export
M 0 "Eastern Kingdoms"
A 87 0 12 0 "Goldshire"
```

`M`: map ID and quoted name. `A`: area ID, map ID, parent area ID, flags,
quoted name. Backslashes and quotes are escaped. Metadata includes sources,
SHA-256, counts and cross-map parent diagnostics in `areas.metadata.json`.
Runtime loads once, validates references/cycles and retains the previous
database on a failed load. Cross-map parent references are preserved: two
occur in the installed Ascension custom table.

`areas.demo.ini` contains Forest/DarkForest/Desert/Snow mappings whose numeric
IDs were discovered from this export. Append those sections to
`EnvironmentProfiles.ini` only after selecting the authoritative database.
Name matching is a tooling convenience; the runtime resolver uses IDs.

To examine one archive without claiming it wins the client's priority rules:

```powershell
python tools/EnvironmentDatabaseBuilder/build_database.py C:\games\Ascension --diagnostic-archive C:\games\Ascension\Data\patch-M.MPQ --output build/client-research/areas.txt
python tools/EnvironmentDatabaseBuilder/inspect_client.py C:\games\Ascension\Ascension.exe
python tools/EnvironmentDatabaseBuilder/test_builder.py
```

The diagnostic database is explicitly marked `diagnostic-only`. It supports
names/validation in F7, but the manager refuses to apply environment modifiers
with this provenance, even if the RVAs have been marked verified.

The inspection script generates `build/client-research/client-inspection.json`
and, for the exact locally examined executable fingerprint,
`LocationProvider.diagnostic.ini`. These RVAs have instruction evidence, **not
live-world validation**. Merge that section into the environment configuration
for diagnostics; do not set `VerifiedInWorld=1` before checking multiple zones,
maps, loading screens and returning to the login screen.
