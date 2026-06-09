import { promises as fs } from "node:fs";
import path from "node:path";
import { NextResponse } from "next/server";
import { liveOutputDir } from "@/lib/demoPaths";

export async function GET() {
  try {
    const text = await fs.readFile(path.join(liveOutputDir(), "storage_tree.txt"), "utf8");
    return new NextResponse(text, {
      headers: { "content-type": "text/plain; charset=utf-8" }
    });
  } catch {
    return new NextResponse("Файловая структура пока не создана.", { status: 404 });
  }
}
