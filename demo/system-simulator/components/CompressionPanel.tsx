import { Archive } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatBytes } from "@/lib/format";

export function CompressionPanel({ state }: { state: LiveState }) {
  const storage = state.storage;

  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center gap-3">
        <Archive className="h-5 w-5 text-sky-700" />
        <h2 className="text-lg font-semibold">Сжатие</h2>
      </div>
      <p className="mt-2 text-sm leading-6 text-slate-600">
        Сравнение: сколько заняли бы точки без сжатия и сколько реально лежит на диске.
      </p>

      <div className="mt-5 space-y-3">
        <Row label="Без сжатия" value={formatBytes(storage.raw_size_estimate_bytes)} hint="Оценка размера: timestamp + value для каждой точки." />
        <Row label="На диске" value={formatBytes(storage.compressed_size_bytes)} hint="Фактический размер segment-файлов этого demo." />
        <Row label="Сжато в" value={`${storage.compression_ratio.toFixed(2)}x`} hint="Во сколько раз меньше места заняли segment-файлы." />
        <Row label="На одну точку" value={`${storage.bytes_per_point.toFixed(1)} Б`} hint="Средний размер одной точки после записи в segment-файлы." />
      </div>
    </section>
  );
}

function Row({ label, value, hint }: { label: string; value: string; hint: string }) {
  return (
    <div className="rounded-lg bg-slate-50 px-4 py-3" title={hint}>
      <div className="flex items-start justify-between gap-4">
        <span className="text-sm text-slate-600">{label}</span>
        <span className="text-right text-sm font-semibold text-slate-950">{value}</span>
      </div>
      <div className="mt-1 text-xs leading-5 text-slate-500">{hint}</div>
    </div>
  );
}
