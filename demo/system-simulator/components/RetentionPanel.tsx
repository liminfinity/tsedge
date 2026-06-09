import { Trash2 } from "lucide-react";
import type { LiveState } from "@/lib/schema";

export function RetentionPanel({ state }: { state: LiveState }) {
  const deleted = state.retention.last_deleted ? humanDeleted(state.retention.last_deleted) : "нет";
  return (
    <section className="panel rounded-xl p-5">
      <div className="flex items-center gap-2">
        <Trash2 className="text-amber-700" size={19} />
        <h2 className="text-base font-semibold">Очистка старых данных</h2>
      </div>
      <div className="mt-4 space-y-2 text-sm">
        <Row label="Очистка" value={state.retention.enabled ? "включена" : "выключена"} />
        <Row label="Последний запуск" value={state.retention.last_run ? "был" : "нет"} />
        <Row label="Удалено файлов" value={String(state.retention.deleted_count)} />
        <Row label="Удалено" value={deleted} />
      </div>
      {state.retention.last_run && state.retention.deleted_count === 0 && (
        <div className="mt-3 rounded-lg bg-amber-50 px-4 py-2 text-sm text-amber-900">
          Полных старых файлов нет.
        </div>
      )}
    </section>
  );
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex min-w-0 justify-between gap-4 rounded-lg bg-slate-50 px-4 py-2">
      <span className="shrink-0 text-slate-500">{label}</span>
      <span className="min-w-0 truncate text-right font-medium">{value}</span>
    </div>
  );
}

function humanDeleted(value: string) {
  return value.includes("segment") ? "старые файлы" : value;
}
