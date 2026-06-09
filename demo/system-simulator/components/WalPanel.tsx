import { ShieldCheck } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber } from "@/lib/format";

export function WalPanel({ state }: { state: LiveState }) {
  return (
    <section className="panel rounded-xl p-5">
      <div className="flex items-center gap-2">
        <ShieldCheck className="text-sky-700" size={20} />
        <h2 className="text-base font-semibold">WAL</h2>
      </div>
      <div className="mt-4 space-y-2 text-sm">
        <Row label="Статус" value="активен" />
        <Row label="Последняя запись" value={state.wal.last_write ? "защищена" : "нет"} />
        <Row label="Ждут записи на диск" value={formatNumber(state.wal.entries_since_flush)} />
        <Row label="Восстановление" value={state.wal.replayed ? "выполнено" : "нет"} />
      </div>
    </section>
  );
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between gap-4 rounded-lg bg-slate-50 px-4 py-2">
      <span className="shrink-0 text-slate-500">{label}</span>
      <span className="min-w-0 truncate text-right font-medium">{value}</span>
    </div>
  );
}
