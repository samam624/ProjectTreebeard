---
name: iterate-until-done
description: Work a debugging or build problem to completion without stopping to narrate between steps. Load when the user says "keep going", "stop stopping", "don't stop", "just fix it", "keep iterating", or when starting any multi-step hardware/firmware debug where the next action is already determined by the last result. Governs when to report and when to just keep working.
---

# Iterate until done

The default failure mode on this repo is stopping after every tool result to explain what just
happened and what might happen next. Each stop costs a full round trip, and the user has to read a
status report to learn something they would have learned faster from the finished result. On a
hardware debug loop where a flash cycle is ~30 seconds, the narration costs more than the work.

## The rule

**If the next action is determined by the result you just got, take it. Do not report first.**

Chain the whole loop: form hypothesis -> build -> flash -> test -> read result -> next hypothesis.
Keep going until one of the stop conditions below is genuinely met.

A failed test is not a stop condition. It is the input to the next iteration.

## Stop only for these

1. **A destructive or outward-facing action needs approval** -- production data deletion, force
   push, anything hard to reverse. Ask once, precisely, with the exact command.
2. **A physical action only the user can take** -- wiring, power cycling, moving hardware.
3. **A genuine fork where the options lead to materially different work** and you cannot pick from
   evidence. Recommend one, do not survey all of them.
4. **The task is done and verified.**
5. **You are out of hypotheses.** Say so plainly and hand over what you measured -- do not keep
   re-running the same test hoping for a different answer.

Everything else -- a failed build, a failed flash, a wrong theory, a surprising measurement -- is
mid-loop. Keep working.

## When you do report

Lead with the state, not the journey.

- What works now (with the number that proves it)
- What does not
- What you are doing next

No preamble, no recap of what you already told them, no restating the plan before executing it.
If a number proves the point, the number IS the report: `29s, was 8min` beats a paragraph.

## Specific anti-patterns seen on this repo

- Announcing a background task, then reporting again when it completes, then reporting the
  analysis. That is three messages for one result. Launch it, wait, report the conclusion.
- Explaining the hypothesis before testing it. Test it, then report what it turned out to be.
- Re-verifying something already confirmed, or re-explaining a correction already made.
- Listing every ruled-out cause mid-loop. Keep them for the final write-up or the doc.
- Asking "want me to keep going?" -- if the next step is obvious, it is not a question.

## Verification is not optional

Iterating fast does not mean claiming success early. A fix is finished when a measurement says so.
Never report a speedup, a fix, or a working feature that has not actually run. If it compiles but
has not been flashed and exercised, say "compiles, not yet proven on hardware" -- and then go prove
it instead of stopping to say that.

## Instrument rather than theorize

When two or three hypotheses fail in a row, stop generating new theories and make the system tell
you. On this project that means device-side diagnostics (`ENABLE_SERIAL_LOGS`, checkpoint records
read back from the application loop) rather than more inference from source. One instrumented build
beats five guesses, and on a 30-second flash cycle it is also faster.

Keep diagnostics out of shipped builds. On MSV3 this is no longer a rule you have to remember:
identity, serial logging and the build tag are build parameters
(`arduino_msv3_probe.ps1 -SensorId <n> [-SerialLogs] [-BuildTag <tag>]`), and the production sketch
`#error`s without `MSV3_SENSOR_ID`. There is no line to edit and none to put back. If you find
yourself editing a constant to identify a board, that is the bug -- pass the parameter instead.
