import { promises as fs } from "node:fs";
import path from "node:path";
import { NextResponse } from "next/server";
import { liveOutputDir } from "@/lib/demoPaths";
import { liveStateSchema } from "@/lib/schema";

export async function GET() {
  const file = path.join(liveOutputDir(), "live_state.json");
  try {
    const text = await fs.readFile(file, "utf8");
    const parsed = liveStateSchema.safeParse(JSON.parse(text));
    if (!parsed.success) {
      return NextResponse.json({ error: "Состояние demo повреждено или устарело." }, { status: 500 });
    }
    return NextResponse.json(parsed.data);
  } catch {
    return NextResponse.json(
      { error: "Состояние demo не найдено. Запустите tsedge_ecopost_agent." },
      { status: 404 }
    );
  }
}
