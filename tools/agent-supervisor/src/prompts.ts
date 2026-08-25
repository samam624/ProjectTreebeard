import type {
  CollaborationPlan,
  Critique,
  ImplementationReport,
  Proposal,
  Review,
} from "./domain.js";

const json = (value: unknown): string => JSON.stringify(value, null, 2);

export function proposalPrompt(task: string, actor: string): string {
  return `PHASE: INDEPENDENT PROPOSAL
You are ${actor}. Analyze the repository for this task without editing any files:

${task}

Produce an independent, concrete implementation proposal. State assumptions, risks, ordered
steps, and observable success criteria. Do not defer to the other agent and do not implement yet.`;
}

export function critiquePrompt(task: string, opponent: string, proposal: Proposal): string {
  return `PHASE: PEER CRITIQUE
Task:
${task}

Review ${opponent}'s independent proposal below. Check it against the repository. Identify useful
ideas, incorrect assumptions, missing edge cases, unsafe actions, and a corrected recommendation.
Do not edit files.

PROPOSAL:
${json(proposal)}`;
}

export function synthesisPrompt(
  task: string,
  claudeProposal: Proposal,
  codexProposal: Proposal,
  claudeCritique: Critique | null,
  codexCritique: Critique | null,
  writer: "claude" | "codex",
): string {
  return `PHASE: PLAN SYNTHESIS
Task:
${task}

Synthesize one final implementation plan from the independent proposals and cross-critiques.
Resolve disagreements explicitly in the chosen approach. Assign every implementation step to
the designated writer, ${writer}. Keep the plan narrow and verifiable. Do not edit files.

CLAUDE PROPOSAL:
${json(claudeProposal)}

CODEX PROPOSAL:
${json(codexProposal)}

CLAUDE CRITIQUE OF CODEX:
${claudeCritique ? json(claudeCritique) : "Not available this run - Claude's critique call did not complete. Use your own judgement in its place."}

CODEX CRITIQUE OF CLAUDE:
${codexCritique ? json(codexCritique) : "Not available this run - Codex's critique call did not complete. Use your own judgement in its place."}`;
}

// Used only when the peer provider's own proposal call failed outright (see
// Orchestrator.runPaired()/the solo-mode branch in runPhases()) - there is no second proposal
// to reconcile against, so this skips straight from one independent proposal to a plan instead
// of paying for a critique/synthesis round that has nothing to critique.
export function soloPlanPrompt(task: string, proposal: Proposal, providerName: "claude" | "codex"): string {
  return `PHASE: PLAN SYNTHESIS (SOLO)
Task:
${task}

The other collaborating agent was unavailable this run, so you are producing the final plan
alone from your own independent proposal below. Turn it into a final implementation plan: keep
it narrow and verifiable, note any risk that would normally have been caught by peer critique,
and assign every step to yourself (${providerName}) as the writer. Do not edit files.

YOUR PROPOSAL:
${json(proposal)}`;
}

export function implementationPrompt(task: string, plan: CollaborationPlan): string {
  return `PHASE: IMPLEMENTATION
Task:
${task}

The user approved write access for this plan:
${json(plan)}

Implement the plan now. You are the only writer. Make focused edits inside the working directory.
Do not run shell commands, tests, package installation, git commit, git push, deployment, or
network operations; the supervisor handles verification. Return an accurate implementation report.`;
}

export function reviewPrompt(
  task: string,
  plan: CollaborationPlan,
  implementation: ImplementationReport,
  testResults: unknown,
  repositoryEvidence: unknown,
  round: number,
): string {
  return `PHASE: READ-ONLY REVIEW ${round}
Task:
${task}

Inspect the repository and current git diff without editing. Determine whether the implementation
meets the approved plan, introduces regressions, misses edge cases, or lacks necessary validation.
Only approve when no critical, high, or medium correctness issue remains.

APPROVED PLAN:
${json(plan)}

WRITER REPORT:
${json(implementation)}

HOST-RUN TEST RESULTS:
${json(testResults)}

HOST-CAPTURED GIT EVIDENCE:
${json(repositoryEvidence)}`;
}

export function fixPrompt(
  task: string,
  plan: CollaborationPlan,
  review: Review,
  round: number,
): string {
  return `PHASE: CORRECTION ${round}
Task:
${task}

Apply only the valid findings in this independent review. Preserve the approved plan and avoid
unrelated cleanup. Do not run shell commands, tests, package installation, git operations,
deployment, or network operations. Return an updated implementation report.

PLAN:
${json(plan)}

REVIEW:
${json(review)}`;
}
