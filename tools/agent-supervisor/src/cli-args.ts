export type ParsedArgs = {
  command: "run" | "doctor" | "help";
  values: Map<string, string[]>;
  flags: Set<string>;
};

export function parseArgs(argv: string[]): ParsedArgs {
  if (argv.includes("--help") || argv.includes("-h")) {
    return { command: "help", values: new Map(), flags: new Set() };
  }
  const first = argv[0];
  const command = first === "doctor" ? "doctor" : first === "run" ? "run" : first ? "help" : "help";
  const values = new Map<string, string[]>();
  const flags = new Set<string>();
  const start = command === "help" ? 0 : 1;

  for (let index = start; index < argv.length; index += 1) {
    const token = argv[index];
    if (!token?.startsWith("--")) continue;
    const key = token.slice(2);
    const parts: string[] = [];

    while (argv[index + 1] && !argv[index + 1]!.startsWith("--")) {
      parts.push(argv[index + 1]!);
      index += 1;
    }

    if (parts.length > 0) {
      values.set(key, [...(values.get(key) ?? []), parts.join(" ")]);
    } else {
      flags.add(key);
    }
  }
  return { command, values, flags };
}
