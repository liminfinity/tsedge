import { FileText } from "lucide-react";
import type { LiveState } from "@/lib/schema";

export function CsvPanel({ state }: { state: LiveState }) {
  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center gap-2">
        <FileText className="text-sky-700" size={19} />
        <h2 className="text-lg font-semibold">Выгрузка CSV</h2>
      </div>
      <p className="mt-2 text-sm leading-6 text-slate-600">
        CSV создаётся из локальных данных, когда связь доступна.
      </p>
      <div className="mt-5 space-y-3 text-sm">
        <Row label="Состояние" value={state.export.csv_ready ? "CSV готов" : "Выгрузки нет"} hint="Готов ли файл для скачивания." />
        <Row label="Файл" value={state.export.last_file ?? "нет"} hint="Последний созданный CSV-файл." />
      </div>
    </section>
  );
}

function Row({ label, value, hint }: { label: string; value: string; hint: string }) {
  return (
    <div className="rounded-lg bg-slate-50 px-4 py-3" title={hint}>
      <div className="flex items-start justify-between gap-4">
        <span className="text-slate-600">{label}</span>
        <span className="break-all text-right font-semibold text-slate-950">{value}</span>
      </div>
      <div className="mt-1 text-xs leading-5 text-slate-500">{hint}</div>
    </div>
  );
}
