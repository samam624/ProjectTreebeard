# Contribution and authorship

Project Treebeard combines hands-on hardware engineering with AI-assisted firmware, software and
analysis tooling. This page states the boundary directly so a reader can evaluate the work without
guessing from commit history.

## Sam Amezcua's work

- Defined the system architecture and hardware requirements across four probe generations and the
  ESP32-S3 receiver.
- Designed the schematics and PCB layouts in Altium, selected parts, prepared fabrication outputs
  and maintained the structured component/BOM library.
- Hand-assembled boards and performed soldering, rework and fault isolation, including fine-pitch
  assembly faults on MSV4.
- Planned and ran bench and field measurements using oscilloscopes, DMMs, SWD tools, current
  measurements, controlled comparisons and pot-mass references.
- Specified firmware behavior and validation criteria, then verified observable behavior on the
  physical hardware and against recorded data.
- Interpreted results, decided hardware revisions and documented which findings were measured,
  inferred or modeled.

## AI-assisted work

Claude Code and Codex produced much of the firmware/software implementation, automation and
analysis code under Sam's requirements and review. Their contribution is preserved in commit
trailers and repository history. Software-debugging stories are not presented as Sam's individual
diagnoses unless the associated record establishes his direct hands-on work.

## How to read the evidence

- A checked-in implementation proves that code exists; it does not by itself prove that Sam wrote
  it or that it ran on hardware.
- A successful build proves compilation only.
- A flash record proves deployment only when the exact artifact and target are identified.
- Hardware behavior is described as observed only when a dated measurement or test record supports
  it.

Start with the [engineering evidence map](docs/portfolio/START_HERE.md) for the shortest path to
schematics, photographs, measurements and bring-up records.
