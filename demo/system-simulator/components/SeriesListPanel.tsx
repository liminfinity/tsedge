import { Database } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatBytes } from "@/lib/format";

export function SeriesListPanel({ state }: { state: LiveState }) {
  const list = state.series_list;
  return (
    <section className="panel rounded-lg p-5">
      <div className="flex items-center justify-between gap-3">
        <div className="flex items-center gap-3">
          <Database size={20} className="text-sky-700" />
          <h2 className="text-xl font-semibold text-slate-950">Временные ряды</h2>
        </div>
        <span className="text-sm text-slate-500">Рядов: {list.count}</span>
      </div>

      {list.status === "error" ? (
        <div className="mt-4 rounded-md bg-rose-50 px-3 py-2 text-sm text-rose-800">
          {list.message ?? "Не удалось получить список рядов"}
        </div>
      ) : (
        <div className="mt-4 overflow-x-auto">
          <table className="w-full min-w-[680px] text-left text-sm">
            <thead className="text-xs uppercase tracking-wide text-slate-500">
              <tr>
                <th className="pb-2 font-medium">Ряд</th>
                <th className="pb-2 font-medium">Точек</th>
                <th className="pb-2 font-medium">Blocks</th>
                <th className="pb-2 font-medium">Segments</th>
                <th className="pb-2 font-medium">Размер</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {list.items.map((item) => (
                <tr key={item.name}>
                  <td className="py-2 font-mono text-slate-950">{item.name}</td>
                  <td className="py-2 text-slate-700">{item.total_points.toLocaleString("ru-RU")}</td>
                  <td className="py-2 text-slate-700">{item.block_count}</td>
                  <td className="py-2 text-slate-700">{item.segment_count}</td>
                  <td className="py-2 text-slate-700">{formatBytes(item.compressed_size_bytes)}</td>
                </tr>
              ))}
            </tbody>
          </table>
          {list.items.length === 0 && <div className="py-4 text-sm text-slate-500">Рядов пока нет.</div>}
        </div>
      )}
    </section>
  );
}
