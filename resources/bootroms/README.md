# SameBoy boot ROMs

The `.bin` files in this directory are the open-source replacement boot ROMs
from the official SameBoy v1.0.4 Windows SDL release, not Nintendo firmware.

Source: https://github.com/LIJI32/SameBoy/tree/v1.0.4/BootROMs

Release: https://github.com/LIJI32/SameBoy/releases/tag/v1.0.4

Archive: `sameboy_winsdl_v1.0.4.zip`

Archive SHA-256 (verified against the release's `SHA2-256SUMS`):
`777a29211e17443435dd0956b7ab775e6e9f40ee859ed4f2f14b43f7587d5a38`

The Expat license in `SameBoy/LICENSE` covers these files. They are embedded
with Qt resources, so users do not need to find or install boot ROM files.
They cover DMG, MGB, CGB, CGB 0, SGB, SGB2 and AGB hardware.
To rebuild them from the submodule, use upstream's BootROMs Makefile targets
with RGBDS installed (`make bootroms`).
