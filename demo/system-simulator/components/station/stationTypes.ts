import type { DemoCommand, LiveState } from "@/lib/schema";

export type Tone = "good" | "warn" | "bad" | "neutral";

export type StationSample = {
  totalPoints: number;
  values: Record<string, number>;
};

export type StationCommandHandler = (command: DemoCommand) => Promise<void>;

export type StationPanelProps = {
  state: LiveState;
  history: StationSample[];
  lastReceivedAt: number | null;
  error: string;
  commandMessage: string;
  commandError: string;
  pendingCommand: DemoCommand | null;
  onCommand: StationCommandHandler;
};
