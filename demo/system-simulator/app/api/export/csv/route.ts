import { promises as fs } from "node:fs";
import path from "node:path";
import { NextResponse } from "next/server";
import { liveOutputDir } from "@/lib/demoPaths";
import { liveStateSchema } from "@/lib/schema";

export async function GET() {
  const dir = liveOutputDir();
  const statePath = path.join(dir, "live_state.json");

  try {
    const stateText = await fs.readFile(statePath, "utf8");
    const parsed = liveStateSchema.safeParse(JSON.parse(stateText));
    if (!parsed.success || !parsed.data.export.csv_ready || !parsed.data.export.last_file) {
      return NextResponse.json({ error: "CSV ещё не готов." }, { status: 404 });
    }

    const filename = parsed.data.export.last_file;
    if (!isSafeCsvFilename(filename)) {
      return NextResponse.json({ error: "Некорректное имя CSV-файла." }, { status: 400 });
    }

    const csvPath = path.join(dir, filename);
    const data = await fs.readFile(csvPath);
    return new Response(data, {
      headers: {
        "content-type": "text/csv; charset=utf-8",
        "content-disposition": `attachment; filename="${filename}"`
      }
    });
  } catch {
    return NextResponse.json({ error: "CSV-файл не найден." }, { status: 404 });
  }
}

function isSafeCsvFilename(filename: string) {
  return /^[A-Za-z0-9_.-]+\.csv$/.test(filename) && !filename.includes("..") && !filename.includes("/") && !filename.includes("\\");
}
