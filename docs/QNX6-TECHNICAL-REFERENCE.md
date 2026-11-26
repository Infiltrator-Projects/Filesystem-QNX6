# QNX6: technical reference

## History and design model

QNX6 Power-Safe appeared in QNX Neutrino 6.4.0 and became the default in 6.4.1. It replaces QNX4's extent-based in-place update model with a copy-on-write publication model. The filesystem keeps two superblock-rooted committed views and prepares the next view without destroying the previous live one. The ordinary profile and Audi MMI 3G variant have different superblock placement/layout. Both little- and big-endian filesystems exist; determine byte order from validated format markers, not host architecture.

## Block geometry and superblock positions

Admitted logical block sizes are 512, 1024, 2048 and 4096 bytes. In the standard profile the boot area is 8192 bytes, primary superblock at 0x2000, reserved superblock area 4096 bytes and logical block zero at 0x3000. The MMI variant starts its first superblock at byte zero, so block zero begins after its reserved area. A superblock's encoded record is 512 bytes even when the reserved region is larger. The second copy is located from validated declared block count and profile geometry, with alignment requirements checked independently. Pointers are 32-bit block indices; 0xffffffff is unused, not an address. Never include a reserved superblock area in the data allocator.

## Standard superblock record

Relative byte offsets for the standard reader layout: magic 0, checksum 4, 64-bit serial 8, creation/access times 16/20, flags 24, two 16-bit versions 28/30, 16-byte volume identifier 32, 32-bit block size 48, inode count 52, free inodes 56, block count 60, free blocks 64, allocation-group parameter 68. Four 80-byte root nodes begin at 72, 152, 232 and 312, representing inode table, bitmap, long-name storage and an additional format-dependent root. The MMI profile has block size at 40 and first root at 64; do not parse it with the standard offsets. Validate both superblock checksums and compare matching volume identity and geometry; selecting solely the highest serial without structural validation risks a corrupt view.

## Root nodes and indirect addressing

A root node has 64-bit size at byte 0, sixteen 32-bit block pointers at 8..71, addressing depth at 72, mode at 73 and six spare bytes. Data lives at leaves only. At depth d, each of the sixteen root pointers spans (block_bytes/4)^d data blocks. To find logical block k, select its top pointer by division by that span, then descend through each indirect block using the successive quotient/remainder of the remaining index. Check every pointer, level, capacity, multiplication and actual read length. In the documented reader, depth is limited to five. An all-ones unused pointer cannot be followed as a real block. Missing leaves require exact native semantics, not fabricated data.

## Inode table and record fields

Each inode is 128 bytes and numbering begins at one; root is inode 1. Inode n resides at byte offset 128*(n-1) in the inode-table tree, not at a fixed raw disk offset. Relative fields: 64-bit size at 0, 32-bit UID/GID 8/12, creation/modify/access/change times 16/20/24/28, 16-bit POSIX mode and extended mode 32/34, sixteen 32-bit block pointers 36..99, file addressing depth 100, status 101, remaining bytes reserved or revision-specific. Treat unknown mode and status combinations cautiously. The inode's block tree uses the same addressing principles as a root node. Preserve metadata and distinguish symbolic links, hard-link semantics and ordinary data.

## Worked indirect lookup

For a 1024-byte filesystem block, an indirect block contains 256 pointers. A depth-one file inode covers up to 16*256 logical blocks. Logical block 600 uses root pointer index 2 and indirect index 88. If that leaf points to block 4200 on standard media, the partition-relative start is 0x3000+4200*1024 = 4313088 bytes. Add the byte offset within the logical block, checking file EOF before transfer. At depth two, each root pointer spans 65536 logical blocks; perform two bounded indirect reads. This is a mapping example, not a claim that every file fills the tree.

## Directory and long-name records

A normal directory consists of 32-byte records: 32-bit inode number at 0, name length at 4 and up to 27 name bytes from 5. A length of 0xff selects the long-name record format: three intervening bytes at 5..7, long-name reference at 8 and checksum field at 12. The long-name tree is separate from the inode table, with references numbered from zero. A leaf contains 16-bit byte length followed by name bytes, with up to 510 bytes supported by the documented format. Bound name length to both record and block capacity; verify profile-specific long-name checksum rules and reject invalid embedded separators. Dot and dot-dot must reference self and parent; graph traversal must avoid loops.

## Allocation bitmap and system area

The active superblock's bitmap tree maps one bit to each filesystem block. Padding bits outside actual volume capacity are allocated. The system region is split into two halves, one for each superblock's system metadata. Bitmap and inode tree blocks and their indirect blocks are maintained in the respective system half. Ordinary file data uses the remaining area. Never decide a block is reusable solely from the prospective bitmap: blocks reachable through the committed view must remain intact until publication. Compare both roots' metadata ownership and exact revision rules when deciding reclamation.

## Publication, consistency and recovery

The writer builds modified payload and metadata in unused storage, stages the next system-tree state under the inactive superblock, and flushes those blocks before publishing the new superblock with advanced serial and valid checksum. A crash before publication leaves the old root authoritative; a valid newer committed root selects the new state. Device flush ordering and torn-write behavior must be tested, not assumed from the existence of two copies. Validate both complete candidates independently, never splice roots across generations, and report ambiguous or unsupported serial/geometry states. Native Power-Safe snapshots here are internal consistency views; do not equate them to arbitrary user-visible retained snapshots.

## Formatting, mutation and defragmentation

A formatter must choose endian profile and block size, lay out boot/reserved regions, both superblocks and their system halves, initialize inode/bitmap/name trees and root namespace, and publish validated initial commit state. Mutation replaces affected leaves and all paths to the inactive roots while preserving the live view. Deletion and relocation cannot reclaim blocks still referenced by the previous stable view. Defrag plans must use actual current ownership and CoW space overhead, copy payload, verify final mappings, and commit the corresponding root generation. In-place metadata or data updates that bypass publication violate Power-Safe's design. Exact native formatting and transactional writer details must be established against the original QNX version.

## Engineering validation and evidence

Exercise both byte orders, standard and MMI layouts, 512/1024/2048/4096-byte blocks, direct and multi-level indirect trees, missing pointers, out-of-range leaves, incompatible geometry, short and long names, deleted entries, invalid inode references, bitmap padding and conflicting superblocks. Inject failures before each data flush, header publication and after commit. Independent mkqnx6fs images and native QNX reading/checking are the interoperability oracle. Source-level reader parity is not writer or physical crash qualification. Original QNX4 media must be rejected rather than interpreted using QNX6 structures.

## References and implementation boundary

Original QNX Power-Safe architecture: https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/fsys.html . Reader format notes: https://docs.kernel.org/filesystems/qnx6.html . Layout cross-check: https://codebrowser.dev/linux/linux/include/linux/qnx6_fs.h.html ; implementation: https://github.com/torvalds/linux/tree/v6.12/fs/qnx6 . Exact checksum polynomial/coverage, MMI long-name validation, all superblock flags and writer-specific allocation rules require primary source/fixture validation. The repository's historical migration and design files define intended Common V2 support, not confirmed operational support.

The PDF preserves both earlier documents as explicitly historical appendices.
