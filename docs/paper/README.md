# MSV papers -- local LaTeX editing

The generic LaTeX tooling (compile script, figure-import script, VS Code setup instructions) lives
outside this repo at `C:\Users\samca\Desktop\latex-tools\` -- it's not specific to the moisture
sensor project and is meant to be reused on other projects too. See that folder's `README.md` for
the full compile/import/VS-Code-setup workflow.

This file just covers what's specific to the papers that actually live in this folder.

## Editing in VS Code

`.vscode/settings.json` (gitignored, local to this machine) already has LaTeX Workshop configured
for this repo -- open `MSV_Evolution_IEEE_Paper.tex`, save, and it auto-compiles with a live PDF
preview tab. See `Desktop\latex-tools\README.md` for the exact settings if you need to recreate
them on another machine.

## Compiling from the command line

```powershell
powershell -ExecutionPolicy Bypass -File C:\Users\samca\Desktop\latex-tools\tex_compile.ps1 -TexFile docs\paper\MSV_Evolution_IEEE_Paper.tex
```

## Importing images as figures

```powershell
powershell -ExecutionPolicy Bypass -File C:\Users\samca\Desktop\latex-tools\tex_import_figures.ps1 -SourceFolder "C:\path\to\folder" -TexDir docs\paper
```

Then tell Claude what each image shows and roughly where it belongs -- see
`Desktop\latex-tools\README.md` for why this step isn't templated.

## This paper's own status

`MSV_Evolution_IEEE_Paper.tex` is the IEEE-conference-format writeup of the MSV1/MSV2/MSV3
platform evolution. As of 2026-08-29 it compiles clean (3 pages, zero errors, zero unresolved
references) but has NOT been proofread/finalized by the author -- see the "AUTHOR VERIFICATION
CHECKLIST" comment block at the end of the `.tex` source before submitting anywhere. It was
converted from `docs/MSV_EVOLUTION_IEEE_PAPER_DRAFT.md` and cross-checked against
`docs/MSV_BUILD_MILESTONE_FACT_CHECK.md`, with one factual correction made in the process: earlier
project docs had the Alocasia probe's electrode topology backwards (see the correction note inline
in `\S II.B` of the `.tex`).
