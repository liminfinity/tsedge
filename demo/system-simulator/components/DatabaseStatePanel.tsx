import { Database } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber, formatPercent } from "@/lib/format";

export function DatabaseStatePanel({ state }: { state: LiveState }) {
  const totalBlocks = state.segments.reduce((sum, segment) => sum + segment.blocks, 0);
  const activeSegment = state.segments.find((segment) => segment.status === "active")?.name ?? "нет";
  const bufferPercent = Math.max(0, Math.min(100, state.buffer.percent));

  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center gap-3">
        <Database className="h-5 w-5 text-sky-700" />
        <h2 className="text-lg font-semibold">Состояние базы</h2>
      </div>

      <div className="mt-5 grid gap-3 sm:grid-cols-2">
        <Metric
          label="Точек записано"
          value={formatNumber(state.agent.total_points_written)}
          hint="Сколько точек C-agent уже передал в TSEdge."
        />
        <Metric
          label="Буфер"
          value={`${formatNumber(state.buffer.points)} / ${formatNumber(state.buffer.capacity)}`}
          hint={`В памяти сейчас ${formatPercent(bufferPercent)} буфера.`}
        />
        <Metric
          label="WAL ждёт flush"
          value={formatNumber(state.wal.entries_since_flush)}
          hint="Записи защищены WAL, но ещё не сброшены в segment-файлы."
        />
        <Metric
          label="Blocks"
          value={formatNumber(totalBlocks)}
          hint="Сжатые пачки точек внутри segment-файлов."
        />
        <Metric
          label="Segment-файлов"
          value={formatNumber(state.segments.length)}
          hint="Файлы, в которых TSEdge хранит blocks на диске."
        />
        <Metric
          label="Активный segment"
          value={activeSegment}
          hint="Файл, куда попадёт следующий block."
        />
      </div>
    </section>
  );
}

function Metric({ label, value, hint }: { label: string; value: string; hint: string }) {
  return (
    <div className="rounded-lg border border-slate-200 bg-white px-4 py-3" title={hint}>
      <div className="text-sm text-slate-600">{label}</div>
      <div className="mt-1 break-words text-xl font-semibold text-slate-950">{value}</div>
      <div className="mt-2 text-xs leading-5 text-slate-500">{hint}</div>
    </div>
  );
}
