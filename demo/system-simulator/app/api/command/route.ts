import { promises as fs } from "node:fs";
import path from "node:path";
import { NextResponse } from "next/server";
import { liveOutputDir } from "@/lib/demoPaths";
import { commandSchema, liveStateSchema } from "@/lib/schema";

export async function POST(request: Request) {
  const body = await request.json().catch(() => null);
  const parsed = commandSchema.safeParse(body);
  if (!parsed.success) {
    return NextResponse.json({ error: "Неизвестная команда." }, { status: 400 });
  }

  const dir = liveOutputDir();
  if (parsed.data.command === "export_csv") {
    const statePath = path.join(dir, "live_state.json");
    const stateText = await fs.readFile(statePath, "utf8").catch(() => null);
    const state = stateText ? parseLiveState(stateText) : null;
    if (state?.success && !state.data.network.online) {
      return NextResponse.json({ error: "Выгрузка недоступна: нет связи." }, { status: 409 });
    }
  }

  const finalPath = path.join(dir, "command.json");
  const tmpPath = path.join(dir, "command.json.tmp");
  const commandPayload: { command: string; created_at: number; series?: string; window_size?: number; points?: number } = {
    command: parsed.data.command,
    created_at: Date.now()
  };
  if (
    (
      parsed.data.command === "export_csv" ||
      parsed.data.command === "aggregate_avg" ||
      parsed.data.command === "aggregate_min_max" ||
      parsed.data.command === "window_aggregate"
    ) &&
    parsed.data.series
  ) {
    commandPayload.series = parsed.data.series;
  }
  if (parsed.data.command === "window_aggregate" && parsed.data.window_size !== undefined) {
    commandPayload.window_size = parsed.data.window_size;
  }
  if (parsed.data.command === "append_custom" && parsed.data.points !== undefined) {
    commandPayload.points = parsed.data.points;
  }
  const payload = JSON.stringify(
    commandPayload,
    null,
    2
  );

  try {
    await fs.mkdir(dir, { recursive: true });
    await fs.writeFile(tmpPath, payload, "utf8");
    await fs.rename(tmpPath, finalPath);
    return NextResponse.json({ ok: true });
  } catch {
    return NextResponse.json({ error: "Не удалось записать command.json." }, { status: 500 });
  }
}

function parseLiveState(text: string) {
  try {
    return liveStateSchema.safeParse(JSON.parse(text));
  } catch {
    return null;
  }
}
