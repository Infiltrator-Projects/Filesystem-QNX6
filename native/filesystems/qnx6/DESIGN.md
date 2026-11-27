# QNX6 — native filesystem design

> **Historical design and migration evidence.** This is the original extraction plan, not the current implementation, capability or release record. The [unified technical reference](../../../docs/QNX6-TECHNICAL-REFERENCE.md) and [installed manual](../../../docs/QNX6.pdf) own the format description; the [repository README](../../../README.md) and exact-revision tests establish provider status. Preserve the pinned evidence below for developers.

## Format scope

QNX6 Power-Safe filesystem, distinct from QNX4. This is a design contract, not a claim of implemented capabilities.

## On-disk structures and ownership

Endian-aware superblocks, alternative metadata roots, generation/serial state, inode and directory trees, allocation metadata and block mapping. The single native C/C++ engine owns these format semantics; consumers provide bounded Common V2 host services, never competing parsers.

## Integrity and mutation invariants

Validate supported layout variants, superblock consistency, checksums and all indirect addresses. Select a valid committed metadata state; do not silently repair inconsistent Power-Safe metadata. Writes require proven transaction semantics. All arithmetic, offsets and lengths are checked before I/O. Writable operations require exclusive access, explicit ordering, flush and interruption/recovery evidence. Read-only operations leave the medium unchanged.

## Common V2 boundary

Only `infiltrator_filesystem_api_v2` is public. Target the genuine PROBE/MOUNT/UNMOUNT/STAT/LIST/READ operations first; negotiate actual supported operation masks. VOLUME, METADATA and DEFRAG operations (including MAP, PLAN, EXECUTE, VERIFY, RECOVER) are individually qualified or refused. Test Media uses the same provider's qualified format and namespace operations. No FUSE, legacy ABI, private driver or kernel proxy. Hosted and import-free freestanding profiles share the format engine, with generic environment-specific host services.

## Qualification

Use independent Power-Safe images, supported endian/layout variants, alternate roots and corrupted generations; compare namespace and bytes against an independent reader. Compare file bytes against an independent oracle, hash unchanged read-only images, inject short I/O, corruption and interruption; validate exact published module separately in BOOT and kernel. Use disposable media only. No new independent binary or test pass is claimed.

## Pinned source-level migration evidence

[Original editable source](https://github.com/Infiltrator-Projects/Filesystems/blob/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/qnx6/linux/inode.c) (blob 3310d1ad4d…). The inherited source explicitly decodes superblock magic, checksum and blocksize using detected disk byte order, then uses separate inode/long-name state. Establish correct selection of the committed Power-Safe superblock and alternate roots, including MMI variants only when proven. Validate indirect pointers and long-file records against media geometry; do not choose an inconsistent generation merely because its serial is newer.

## Common V2 engineering and release acceptance

- A valid CORE descriptor with a real PROBE is mandatory. Every optional operation has its own capability bit and non-null qualified callback; unavailable operations have neither. Recognised format, unsupported feature, corruption, host I/O failure and stale handles remain distinct outcomes.
- Use Common's sector-aligned bounded byte helpers. Validate partition extent, logical/physical sector geometry and maximum transfer before reading. Serialize overlapping read/modify/write sectors; request explicit flush barriers. Never infer exclusivity from flags alone; require real exclusive unmounted ownership before formatting or raw mutation. Require the Common entropy service when the format needs a unique identity.
- Qualify hosted and import-free freestanding profiles separately; enforce module identity and trust, exact export set and actual packaged bytes. BOOT and kernel are generic loaders; BOOT uses the same file in `/EFI/InfiltratorOS/BOOT/drivers_x64/`, with the installed canonical copy in `/System/Filesystems/`. No per-format .ko/.efi, FUSE, proxy, legacy ABI, external formatter or consumer-side parsing substitutes for the engine.
- Test Media must format and populate through actual V2 FORMAT/CREATE/WRITE operations; an independent formatter/reader may be a test oracle only, never production fallback. Defragmentation maps, plans, execution, verification and recovery require independent proof of ownership, data preservation and interruption safety. Where not implemented they remain absent.
- New build and implementation code is native C/C++ and shell as appropriate, no Python. The generic amd64 DEB and CPU-native source-downloading RUN are the only manually published release assets; GitHub generates source archives. RUN fetches exact versioned source and pinned Common dependency, verifies integrity, compiles natively and installs the selected package; no source archive or DEB embedded. Release immutable exact-main qualification only. Release availability is established by exact-revision release records, not this historical plan.

## Source and migration

[Central source and historical design, pinned revision](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/qnx6). Adapt the retained implementation without erasing inherited notices or licences. New project-authored code uses GPL-3.0-or-later. See [migration status](../../../MIGRATION_STATUS.md), [philosophy](../../../docs/PHILOSOPHY.md) and [Common V2 contract](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md). The migration plan records the original baseline; consult current source and qualification for its outcome.
