import { useEffect, useMemo, useState } from "react";
import { Download } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber } from "@/lib/format";

export function ExportCsvAction({
  state,
  pending,
  message,
  error,
  onExport
}: {
  state: LiveState;
  pending: boolean;
  message: string;
  error: string;
  onExport: (series: string) => void;
}) {
  const seriesNames = useMemo(() => state.series_list.items.map((item) => item.name), [state.series_list.items]);
  const [selectedSeries, setSelectedSeries] = useState(seriesNames[0] ?? "air.temperature");

  useEffect(() => {
    if (seriesNames.length > 0 && !seriesNames.includes(selectedSeries)) {
      setSelectedSeries(seriesNames[0]);
    }
  }, [seriesNames, selectedSeries]);

  const offline = !state.network.online;
  const noSeries = seriesNames.length === 0;
  const disabled = pending || state.agent.status === "crashed" || offline || noSeries || !selectedSeries;
  return (
    <div className="mt-4 rounded-xl border border-slate-200 bg-white p-4">
      <div className="grid gap-4 lg:grid-cols-[1fr_auto] lg:items-end">
        <div>
          <div className="font-medium text-slate-950">CSV-выгрузка</div>
          <label className="mt-3 block">
            <span className="mb-1.5 block text-sm text-slate-600">Ряд</span>
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
        </div>
        <button
          type="button"
          disabled={disabled}
          onClick={() => onExport(selectedSeries)}
          className="inline-flex min-h-11 items-center justify-center gap-2 rounded-lg bg-sky-600 px-4 py-2 text-sm font-medium text-white transition hover:bg-sky-700 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:bg-slate-300"
        >
          <Download size={16} />
          {offline ? "Нет связи" : pending ? "Выгружаю..." : "Выгрузить CSV"}
        </button>
      </div>
      <div className="mt-3 text-sm text-slate-600">
        {state.export.ok
          ? `Экспортировано строк: ${formatNumber(state.export.rows)}`
          : offline
            ? "Выгрузка доступна после восстановления связи."
            : noSeries
              ? "Нет рядов для выгрузки."
              : "Выберите ряд и выгрузите CSV."}
      </div>
      {(message || error) && (
        <div className={`mt-3 rounded-lg px-3 py-2 text-sm ${error ? "bg-rose-50 text-rose-800" : "bg-emerald-50 text-emerald-800"}`}>
          {error || message}
        </div>
      )}
      {state.agent.status === "crashed" && (
        <div className="mt-3 rounded-lg bg-rose-50 px-3 py-2 text-sm text-rose-800">
          Сначала восстановите станцию.
        </div>
      )}
      {offline && (
        <div className="mt-3 rounded-lg bg-amber-50 px-3 py-2 text-sm text-amber-800">
          Восстановите связь, чтобы выгрузить CSV.
        </div>
      )}
      {state.export.csv_ready && state.export.last_file && !offline && (
        <a
          href="/api/export/csv"
          className="mt-3 inline-flex min-h-10 items-center justify-center rounded-lg border border-sky-200 bg-sky-50 px-4 py-2 text-sm font-medium text-sky-800 transition hover:bg-sky-100 focus-visible:ring-2 focus-visible:ring-sky-400"
        >
          Скачать CSV
        </a>
      )}
    </div>
  );
}
