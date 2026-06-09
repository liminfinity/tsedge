import { Trash2 } from "lucide-react";
import type { LiveState } from "@/lib/schema";

export function RetentionPanel({ state }: { state: LiveState }) {
  const deleted = state.retention.last_deleted ? humanDeleted(state.retention.last_deleted) : "нет";
  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center gap-2">
        <Trash2 className="text-amber-700" size={19} />
        <h2 className="text-lg font-semibold">Очистка старых данных</h2>
      </div>
      <p className="mt-2 text-sm leading-6 text-slate-600">
        Удаляются только старые segment-файлы целиком.
      </p>
      <div className="mt-5 space-y-3 text-sm">
        <Row label="Состояние" value={state.retention.enabled ? "включена" : "выключена"} hint="Можно запускать очистку старой истории." />
        <Row label="Последний запуск" value={state.retention.last_run ? "был" : "нет"} hint="Показывает, запускали ли очистку в этой сессии." />
        <Row label="Удалено файлов" value={String(state.retention.deleted_count)} hint="Сколько segment-файлов удалено последним запуском." />
        <Row label="Что удалено" value={deleted} hint="Если старых файлов нет, база ничего не удаляет." />
      </div>
      {state.retention.last_run && state.retention.deleted_count === 0 && (
        <div className="mt-4 rounded-lg bg-amber-50 px-4 py-3 text-sm leading-6 text-amber-900">
          Полных старых файлов нет.
        </div>
      )}
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

function humanDeleted(value: string) {
  return value.includes("segment") ? "старые файлы" : value;
}
