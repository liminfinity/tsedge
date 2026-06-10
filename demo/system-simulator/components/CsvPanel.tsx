"use client";

import { useEffect, useMemo, useState } from "react";
import { Download, FileText } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber } from "@/lib/format";

const fallbackSeries = [
  "air.temperature",
  "air.humidity",
  "air.pressure",
  "wind.speed",
  "pm25.concentration",
  "battery.voltage",
  "debug.temp"
];

export function CsvPanel({
  state,
  pending = false,
  onExport
}: {
  state: LiveState;
  pending?: boolean;
  onExport?: (series: string) => void;
}) {
  const seriesNames = useMemo(() => {
    const names = state.series_list.items.map((item) => item.name);
    return names.length > 0 ? names : fallbackSeries;
  }, [state.series_list.items]);
  const [selectedSeries, setSelectedSeries] = useState(seriesNames[0] ?? "");

  useEffect(() => {
    if (!seriesNames.includes(selectedSeries)) {
      setSelectedSeries(seriesNames[0] ?? "");
    }
  }, [seriesNames, selectedSeries]);

  const offline = !state.network.online;
  const noSeries = seriesNames.length === 0;
  const disabled = pending || offline || noSeries || !selectedSeries;

  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-start justify-between gap-4">
        <div className="flex items-center gap-2">
          <FileText className="text-sky-700" size={19} />
          <h2 className="text-lg font-semibold">Выгрузка CSV</h2>
        </div>
        {state.export.ok && state.export.last_file && (
          <a
            href="/api/export/csv"
            className="inline-flex items-center gap-2 rounded-lg border border-sky-200 px-3 py-2 text-sm font-semibold text-sky-800 hover:bg-sky-50"
          >
            <Download size={15} />
            Скачать
          </a>
        )}
      </div>
      <div className="mt-5 space-y-4 text-sm">
        <label className="block">
          <span className="mb-1.5 block text-slate-600">Ряд</span>
          <select
            value={selectedSeries}
            disabled={noSeries}
            onChange={(event) => setSelectedSeries(event.target.value)}
            className="h-11 w-full rounded-lg border border-slate-200 bg-white px-3 text-slate-950 outline-none focus:border-sky-400 focus:ring-2 focus:ring-sky-100 disabled:bg-slate-100 disabled:text-slate-400"
          >
            {seriesNames.map((name) => (
              <option key={name} value={name}>
                {name}
              </option>
            ))}
          </select>
        </label>
        <button
          type="button"
          disabled={disabled}
          onClick={() => onExport?.(selectedSeries)}
          className="inline-flex h-11 w-full items-center justify-center gap-2 rounded-lg bg-sky-700 px-4 text-sm font-semibold text-white outline-none transition hover:bg-sky-800 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:bg-slate-300"
        >
          <Download size={16} />
          {offline ? "Нет связи" : pending ? "Выгружаю..." : "Выгрузить CSV"}
        </button>
        {offline && <p className="text-sm text-amber-700">Выгрузка доступна после восстановления связи.</p>}
        {noSeries && <p className="text-sm text-slate-600">Нет рядов для выгрузки.</p>}
        <div className="rounded-lg bg-slate-50 px-4 py-3">
          <div className="flex items-center justify-between gap-4">
            <span className="text-slate-600">Строк в последнем CSV</span>
            <span className="font-semibold text-slate-950">{formatNumber(state.export.rows)}</span>
          </div>
          {state.export.last_run && (
            <div className={`mt-2 text-sm ${state.export.ok ? "text-emerald-700" : "text-rose-700"}`}>
              {state.export.ok ? "CSV готов." : state.export.message ?? "CSV не создан."}
            </div>
          )}
        </div>
      </div>
    </section>
  );
}
