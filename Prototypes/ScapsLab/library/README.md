# Built-in scientific content

`cigs-literature.sstlibrary.yaml` contains the 23 items curated by libdata1 on 2026-11-26, with 209 individually cited nominal parameters. The DOI and PDF page/table are retained on every sourced parameter. These are literature model inputs, not a calibration of a particular laboratory stack.

Omitted fields use engine defaults. They are not literature measurements; the item editor identifies fields without an individual citation. Equal lower/upper values record a transcribed nominal value, not a measured zero uncertainty. Derive an item before editing it.

Two interface items use a named `relative_to_Ec` reference rather than a native SCAPS integer code. The file adapter records code 4 (lower conduction-band edge) and marks the items **review required**. They cannot be applied until the user derives the item and explicitly confirms the reference convention. `density_convention: total` is the dimensionally identical `integrated_sheet` alias. No numerical source values are changed.

The 27 reference-only cards from libdata1 contain interpretations, incomplete distributions and source ranges. They are not converted into solver inputs. No numerical optical arrays or replacement ASTM spectrum are invented.

Research manifest, PDFs, extraction evidence, conflicts and the complete bibliography remain in the owner’s `tools/agents/runs/libdata1/` package. Product distribution includes the cited numerical records, without source PDFs.
