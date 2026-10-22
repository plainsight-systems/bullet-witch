# Community and historical engine research — October 22, 2026

- Followed Rich Whitehouse's 2012 demonstration to official Noesis distribution.
  Saved ZIP, selected reference source and SHA-256 manifest under research only.
- Read CPR/model parser: useful skeleton, material, draw-group and vertex binding
  contracts; same script covers Zegapain XOR. Native motion decoding absent.
- Retrieved pinned Banz99 NieR texture manager source. Recorded its HEAP/TX2D,
  paired-file and LZO architecture; kept original NieR separate from remaster tools.
- Recovered archived Softimage customer interview through Wayback availability API
  and direct urllib retrieval after web-tool failure. Recorded graphics staff,
  custom exporter, authoring workflow and rendering leads in the source note.
- Wrote independent bounded PC metadata probe. Initial run stopped on a legal
  zero-length GENV chunk; adjusted traversal to allow empty leaf chunks.
- Console-style 12-byte vertex declaration interpretation produced inconsistent
  fields. Inspection instead identified 8-byte D3DVERTEXELEMENT9 entries after a
  four-byte prefix. Compared with Microsoft documentation; all nine declarations
  have the expected end marker. Preserved this correction rather than treating
  endian conversion alone as sufficient.
- Final PW0 pass reaches the exact end of header.bin: two chunks, nine VDCL/VTX/IDX
  sets, four shape bindings, fourteen groups, forty-six draw records/bone maps,
  fourteen texture names and 125 skeleton records. Structural evidence only;
  no rendered-model or native animation claim.
- Source game opened read-only. All new files are inside research/.

Results: [report](../reports/2026-10-22-cavia-community-research.md),
[source catalog](../sources/community-reverse-engineering.md),
[model map](../engine/subsystems/model-data.md).
