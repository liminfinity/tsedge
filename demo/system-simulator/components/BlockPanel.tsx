import { FileArchive } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber } from "@/lib/format";

export function BlockPanel({ state }: { state: LiveState }) {
  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center gap-2">
        <FileArchive className="text-sky-700" size={19} />
        <h2 className="text-lg font-semibold">Сжатый block</h2>
      </div>
      <p className="mt-2 text-sm leading-6 text-slate-600">
        Block — это пачка точек. TSEdge сжимает её и записывает в segment-файл.
      </p>
      <div className="mt-5 space-y-3 text-sm">
        <Row label="Новый block" value={state.compression.last_block_created ? "создан" : "нет"} hint="Появился ли block после последнего flush." />
        <Row label="Размер block" value={`${formatNumber(state.compression.last_block_points)} точек`} hint="Сколько точек база старается собрать в одну пачку." />
        <Row label="Сжатие" value={state.compression.active ? "включено" : "нет"} hint="Точки пишутся не сырыми, а в сжатом виде." />
      </div>
    </section>
  );
}

function Row({ label, value, hint }: { label: string; value: string; hint: string }) {
  return (
    <div className="rounded-lg bg-slate-50 px-4 py-3" title={hint}>
      <div className="flex items-start justify-between gap-4">
        <span className="text-slate-600">{label}</span>
        <span className="text-right font-semibold text-slate-950">{value}</span>
      </div>
      <div className="mt-1 text-xs leading-5 text-slate-500">{hint}</div>
    </div>
  );
}
