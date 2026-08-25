# Claude–Codex Agent Supervisor

This local TypeScript CLI gives Claude and Codex a bounded collaboration protocol without making
either model the uncontrolled manager of the other.

The workflow is:

1. Claude and Codex independently inspect the repository and propose a plan in read-only mode.
2. Each critiques the other proposal in read-only mode.
3. The configured leader synthesizes one structured plan.
4. The run stops unless write access was explicitly enabled and the plan was approved.
5. Exactly one configured writer can edit. It cannot use Bash, the network, git, or deployment tools.
6. The host runs only user-configured test commands, with AI-provider credentials removed from the
   test process environment.
7. The other provider reviews in read-only mode. Corrections are bounded by `maxReviewRounds`.
8. Prompts, responses, tests, plans, and summaries are written under `.agent-runs/`.

The supervisor never commits, pushes, deploys, installs dependencies inside the target, or invents
test commands. Test commands are trusted user input and execute through the operating-system shell.

## Project context

Both providers read this repository's own instructions automatically, the same way they would in
an interactive session: Claude loads `CLAUDE.md` (via `settingSources: ["project"]` - earlier
versions passed `[]`, which silently skipped it), and Codex loads the repo-root `AGENTS.md`, its
own equivalent auto-discovered convention. Keep the two files in sync; each says so at the top.

This matters beyond correctness (a provider that doesn't know a project rule can recommend
violating it) - without it, every phase had to rediscover basic repository conventions via tool
calls from a blank slate, which was a real driver of the turn/budget exhaustion seen in past runs.
Neither file is a substitute for a well-scoped `--task`: this tool has no access to anything said
in your own interactive Claude Code session, so include whatever conversational context actually
matters (`--task-file` is easiest for anything longer than a sentence).

## Install

From this directory:

```powershell
npm.cmd install
npm.cmd run check
npm.cmd test
```

Both agent SDK packages include their Windows binaries. Authentication is still required for live
runs. Check readiness without revealing credential values:

```powershell
npm.cmd run doctor
```

Run `codex login` and `claude auth login` with the subscription accounts you intend to use. Live
runs intentionally reject API keys, Anthropic Console authentication, Bedrock, Vertex, Foundry,
custom billing endpoints, and other known metered routes. Do not put credentials in this repository.

## Billing safety

The supervisor is subscription-only and fails closed:

- Known metered authentication variables cause the run to stop before either provider starts.
- Metered credentials are also removed from each provider child process environment.
- Codex is forced to standard speed with Fast mode disabled.
- Claude's paid-only 1M context aliases are rejected, and the child process disables both 1M
  context and the command for enabling usage credits.
- A genuine usage/session/billing-limit error from either provider (matched against wording like
  "usage limit", "session limit", "quota", "credit balance") immediately aborts *all* concurrent
  and future work in the run - that stop is unconditional and is the actual billing backstop.
- An ordinary failure that is *not* a billing signal (a turn-limit hit, a malformed response, a
  timed-out call) only takes that one provider out of that one phase. The run degrades to
  whatever the other provider produced instead of discarding it - see "Resilience" below.
- Every provider call also has a hard wall-clock timeout (`providerCallTimeoutSeconds`, default
  900s), enforced by the supervisor itself rather than relying on either SDK's own turn/budget
  accounting to eventually give up.
- Claude's per-call and total client-side budget estimates remain enabled as a secondary stop.

## Resilience

Earlier versions aborted the *entire* run the moment either provider hit any error, including
ordinary ones like a turn-limit or a single malformed JSON response - concurrent work from the
other provider (already paid for) was discarded, and a failed run left no artifact beyond a raw
event log. That is why runs regularly "used tokens and produced nothing."

Now:

- The initial proposal and critique calls run concurrently as before, but a non-billing failure
  on one side no longer cancels the other. If exactly one provider produces a proposal, the run
  continues in solo mode: the surviving provider synthesizes its own plan and (if write access is
  approved) implements and is reviewed by the other provider once it becomes available again for
  a call, rather than the run failing outright.
- If a provider has already failed a call this run, it is not handed the leader/writer/reviewer
  role for later phases by default - the other provider takes over, with a progress line noting
  the fallback.
- If the independent review call itself fails (not a billing stop), the run still returns
  `needs_attention` with an explicit note that review could not be completed, instead of
  throwing away a completed implementation.
- Any run that still ends in failure writes `failure.md` under its `.agent-runs/<run>/` directory
  and includes that path in the thrown error, so there is always something to look at.

The runner cannot read or override billing choices stored in your Claude or ChatGPT account. For a
no-extra-charge guarantee, disable Claude usage credits from `/usage-credits` in a normal interactive
Claude session and make sure additional Codex credits or automatic credit purchases are disabled in
your ChatGPT/Codex billing settings. If provider-side paid credits remain enabled, the provider may
consume them without sending the runner a usage-limit error, which no local process can detect in
advance.

## First run

Exercise the complete state machine without API calls or file edits:

```powershell
npm.cmd run demo
```

Run a live read-only planning session:

```powershell
npm.cmd run start -- --task "Diagnose the BLE reconnect regression and propose the smallest fix"
```

The plan and full transcript are saved, but no provider receives write access.

## Live status

Runs print phase updates to the terminal by default. Each provider call reports when it starts and
finishes, and prints a heartbeat every 15 seconds while it is still working. The output describes
the current operation without exposing private model reasoning. For example:

```text
[18:54:29] STARTED   Claude started analyzing the repository and drafting a proposal (read-only access).
[18:54:29] STARTED   Codex started analyzing the repository and drafting a proposal (read-only access).
[18:54:44] WORKING   Claude is still analyzing the repository and drafting a proposal. (15s)
[18:54:51] COMPLETED Codex finished analyzing the repository and drafting a proposal. (22s)
```

The first update also prints the live `events.jsonl` path. In another PowerShell window, you can
follow that durable event log with:

```powershell
Get-Content -Wait "<events.jsonl path printed by the run>"
```

Pass `--quiet` to suppress terminal status updates. Final plans and summaries remain under
`.agent-runs/`.

## Resuming your existing conversations

`--resume-claude` and `--resume-codex` pick up an existing conversation instead of starting each
provider blank. Pass `auto` (or the bare flag) to find your most recent real conversation on this
repo automatically -- whether it was driven from a terminal CLI or an editor extension's chat tab,
both land in the same resumable session/thread store, so either works the same way:

```powershell
npm.cmd run start -- --task "..." --resume-claude auto --resume-codex auto
```

Auto-detection deliberately excludes any session/thread this supervisor already used on a
previous run in this repo (tracked via this run's own `.agent-runs/` logs, plus Codex threads
tagged with this SDK's own `codex_sdk_ts` originator) so it keeps pointing at *your* conversation,
not a branch the supervisor already grew off of it. That also means a resumed session drops out of
future `auto` picks once used once -- pass its exact id instead of `auto` if you want the same
thread reused across multiple supervisor runs.

Resuming is not free: every call in the resumed session/thread carries its prior transcript as
input context. A session you were actively using (its prompt-cache is still warm) costs much less
for the cached portion than reprocessing it at full price, but an idle-for-hours one may not still
benefit from that discount, and either way it's still more than starting blank. For a short, focused
context transfer, a `--task-file` brief is usually cheaper and just as effective -- resuming is for
when the *shape* of an existing conversation (not just its conclusions) actually matters.

Run an interactive implementation after configuring trusted tests:

```powershell
npm.cmd run start -- `
  --task "Fix the BLE reconnect regression" `
  --allow-write `
  --test "python -m pytest tests"
```

After synthesis, the supervisor asks before granting the configured writer access. For unattended
operation, both `--allow-write` and `--yes` are required:

```powershell
npm.cmd run start -- `
  --task-file .\task.md `
  --allow-write `
  --yes `
  --test "python -m pytest tests"
```

Use `--allow-dirty` only after reviewing existing local changes. By default, a write-enabled run
refuses to start in a dirty worktree.

## Configuration

Copy `agent-supervisor.config.example.json` to an untracked location and pass it with `--config`.
CLI values for root, leader, writer, tests, and review rounds override the JSON settings.

Useful commands:

```powershell
npm.cmd run doctor
npm.cmd run demo
npm.cmd run start -- --task "..."
npm.cmd run start -- --help
```
