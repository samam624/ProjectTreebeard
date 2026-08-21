import { z } from "zod";

const NonEmptyString = z.string().trim().min(1);

export const ProviderNameSchema = z.enum(["claude", "codex"]);
export type ProviderName = z.infer<typeof ProviderNameSchema>;

export const ProposalSchema = z.object({
  summary: NonEmptyString,
  assumptions: z.array(NonEmptyString),
  steps: z.array(
    z.object({
      id: NonEmptyString,
      title: NonEmptyString,
      details: NonEmptyString,
      verification: NonEmptyString,
    }),
  ).min(1),
  risks: z.array(NonEmptyString),
  successCriteria: z.array(NonEmptyString).min(1),
});
export type Proposal = z.infer<typeof ProposalSchema>;

export const CritiqueSchema = z.object({
  overallAssessment: NonEmptyString,
  strengths: z.array(NonEmptyString),
  concerns: z.array(NonEmptyString),
  corrections: z.array(NonEmptyString),
  revisedRecommendation: NonEmptyString,
});
export type Critique = z.infer<typeof CritiqueSchema>;

export const PlanSchema = z.object({
  objective: NonEmptyString,
  chosenApproach: NonEmptyString,
  assumptions: z.array(NonEmptyString),
  steps: z.array(
    z.object({
      id: NonEmptyString,
      title: NonEmptyString,
      details: NonEmptyString,
      owner: ProviderNameSchema,
      verification: NonEmptyString,
    }),
  ).min(1),
  risks: z.array(NonEmptyString),
  successCriteria: z.array(NonEmptyString).min(1),
});
export type CollaborationPlan = z.infer<typeof PlanSchema>;

export const ImplementationReportSchema = z.object({
  summary: NonEmptyString,
  filesChanged: z.array(NonEmptyString),
  testsRecommended: z.array(NonEmptyString),
  remainingRisks: z.array(NonEmptyString),
});
export type ImplementationReport = z.infer<typeof ImplementationReportSchema>;

export const ReviewSchema = z.object({
  approved: z.boolean(),
  summary: NonEmptyString,
  findings: z.array(
    z.object({
      severity: z.enum(["critical", "high", "medium", "low"]),
      file: z.string(),
      issue: NonEmptyString,
      recommendation: NonEmptyString,
    }),
  ),
  testsSuggested: z.array(NonEmptyString),
});
export type Review = z.infer<typeof ReviewSchema>;

export type RunStatus = "planned" | "approved" | "needs_attention";

export function jsonSchemaFor(schema: z.ZodType): Record<string, unknown> {
  // Claude Agent SDK validates structured output schemas as JSON Schema draft-07.
  // Zod defaults to draft 2020-12, which Claude rejects before starting a run.
  return z.toJSONSchema(schema, { target: "draft-7" }) as Record<string, unknown>;
}

export function parseJsonText(text: string): unknown {
  const trimmed = text.trim();
  const fenced = trimmed.match(/^```(?:json)?\s*([\s\S]*?)\s*```$/i);
  const candidate = fenced?.[1] ?? trimmed;

  try {
    return JSON.parse(candidate);
  } catch {
    const start = candidate.indexOf("{");
    const end = candidate.lastIndexOf("}");
    if (start >= 0 && end > start) {
      return JSON.parse(candidate.slice(start, end + 1));
    }
    throw new Error("Provider response did not contain a JSON object.");
  }
}
