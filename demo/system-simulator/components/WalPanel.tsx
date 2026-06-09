import { ShieldCheck } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber } from "@/lib/format";

export function WalPanel({ state }: { state: LiveState }) {
  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center gap-2">
        <ShieldCheck className="text-sky-700" size={20} />
        <h2 className="text-lg font-semibold">Журнал WAL</h2>
      </div>
      <p className="mt-2 text-sm leading-6 text-slate-600">
        WAL хранит последние точки до записи в segment-файл.
      </p>
      <div className="mt-5 space-y-3 text-sm">
        <Row label="Состояние" value="активен" hint="Журнал принимает новые записи." />
        <Row label="Последняя запись" value={state.wal.last_write ? "защищена" : "нет"} hint="Точка сначала попадает в WAL, потом в буфер." />
        <Row label="Ждут flush" value={formatNumber(state.wal.entries_since_flush)} hint="Сколько записей ещё не ушло в segment-файлы." />
        <Row label="Восстановление" value={state.wal.replayed ? "выполнено" : "нет"} hint="Был ли replay после сбоя." />
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
