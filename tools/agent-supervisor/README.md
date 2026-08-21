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

For Codex, either run `codex login` or provide `CODEX_API_KEY` for the supervisor process. For
Claude, your own authenticated Claude Code installation can supply the login; alternatively provide
`ANTHROPIC_API_KEY` or configure one of its supported cloud providers. Do not put credentials in
this repository. A product offered to other users should follow Anthropic's separate Agent SDK
authentication policy rather than reusing an individual's Claude subscription login.

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
