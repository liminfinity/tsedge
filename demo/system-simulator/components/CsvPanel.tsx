import { FileText } from "lucide-react";
import type { LiveState } from "@/lib/schema";

export function CsvPanel({ state }: { state: LiveState }) {
  return (
    <section className="panel rounded-xl p-5">
      <div className="flex items-center gap-2">
        <FileText className="text-sky-700" size={19} />
        <h2 className="text-base font-semibold">Выгрузка CSV</h2>
      </div>
      <div className="mt-4 space-y-2 text-sm">
        <Row label="Статус" value={state.export.csv_ready ? "CSV готов" : "Выгрузки нет"} />
        <Row label="Файл" value={state.export.last_file ?? "нет"} />
      </div>
    </section>
  );
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex min-w-0 justify-between gap-4 rounded-lg bg-slate-50 px-4 py-2">
      <span className="shrink-0 text-slate-500">{label}</span>
      <span className="min-w-0 break-all text-right font-medium">{value}</span>
    </div>
  );
}
