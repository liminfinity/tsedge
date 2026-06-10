import { Download } from "lucide-react";
import type { LiveState } from "@/lib/schema";

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
  onExport: () => void;
}) {
  const offline = !state.network.online;
  const disabled = pending || state.agent.status === "crashed" || offline;
  return (
    <div className="mt-4 rounded-xl border border-slate-200 bg-white p-4">
      <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
        <div>
          <div className="font-medium text-slate-950">CSV-выгрузка</div>
          <div className="mt-1 text-sm text-slate-500">
            {offline
              ? "Выгрузка доступна после восстановления связи"
              : state.export.csv_ready
                ? `Готов файл: ${state.export.last_file ?? "CSV"}`
                : "Сохранить данные станции в CSV"}
          </div>
        </div>
        <button
          type="button"
          disabled={disabled}
          onClick={onExport}
          className="inline-flex min-h-10 items-center justify-center gap-2 rounded-lg bg-sky-600 px-4 py-2 text-sm font-medium text-white transition hover:bg-sky-700 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:bg-slate-300"
        >
          <Download size={16} />
          {offline ? "Нет связи" : pending ? "Выгружаю..." : "Выгрузить CSV"}
        </button>
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
