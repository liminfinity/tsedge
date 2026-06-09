import { AlertTriangle, CheckCircle2, Loader2, RotateCcw, SearchCheck, XCircle } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber } from "@/lib/format";

export function MaintenancePanel({ state, checking = false }: { state: LiveState; checking?: boolean }) {
  const verifyStatus = checking
    ? "проверяется"
    : !state.verify.available
      ? "недоступна"
    : !state.verify.last_run
      ? "не запускалась"
      : state.verify.ok
        ? "ошибок нет"
        : "есть ошибка";
  const statusTone = checking
    ? "text-sky-700"
    : !state.verify.available
      ? "text-slate-600"
    : !state.verify.last_run
      ? "text-slate-600"
      : state.verify.ok
        ? "text-emerald-700"
        : "text-rose-700";
  const StatusIcon = checking
    ? Loader2
    : !state.verify.available
      ? AlertTriangle
    : !state.verify.last_run
      ? SearchCheck
      : state.verify.ok
        ? CheckCircle2
        : XCircle;

  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-start justify-between gap-4">
        <div className="flex items-center gap-3">
        <RotateCcw className="h-5 w-5 text-sky-700" />
          <h2 className="text-lg font-semibold">Проверка базы</h2>
        </div>
        <div className={`flex items-center gap-2 rounded-full bg-slate-50 px-3 py-1 text-sm font-semibold ${statusTone}`}>
          <StatusIcon className={`h-4 w-4 ${checking ? "animate-spin" : ""}`} />
          {verifyStatus}
        </div>
      </div>

      {!state.verify.available ? (
        <div className="mt-5 rounded-lg bg-slate-50 px-4 py-3 text-sm leading-6 text-slate-600">
          Проверка базы пока не реализована.
        </div>
      ) : !state.verify.last_run && !checking ? (
        <div className="mt-5 rounded-lg bg-slate-50 px-4 py-3 text-sm leading-6 text-slate-600">
          Нажмите «Проверить базу», чтобы проверить manifest, metadata, WAL и segment-файлы.
        </div>
      ) : (
        <div className="mt-5 grid gap-3 sm:grid-cols-2">
          <Stat label="Рядов" value={formatNumber(state.verify.series_count)} />
          <Stat label="Segment-файлов" value={formatNumber(state.verify.segment_count)} />
          <Stat label="Блоков" value={formatNumber(state.verify.block_count)} />
          <Stat label="Записей WAL" value={formatNumber(state.verify.wal_entry_count)} />
          <Stat
            label="Ошибок"
            value={checking ? "..." : formatNumber(state.verify.error_count)}
            tone={!checking && state.verify.error_count > 0 ? "bad" : "good"}
          />
          <Stat
            label="Очистка старых данных"
            value={state.retention.last_run ? `удалено ${formatNumber(state.retention.deleted_count)}` : "не запускалась"}
          />
        </div>
      )}

      {state.verify.last_run && !state.verify.ok && (
        <div className="mt-4 rounded-lg border border-rose-200 bg-rose-50 px-4 py-3 text-sm leading-6 text-rose-800">
          <div className="flex items-center gap-2 font-semibold">
            <AlertTriangle className="h-4 w-4" />
            Найдена проблема
          </div>
          <div className="mt-2 break-words">{state.verify.first_error_message ?? "База повреждена."}</div>
          {state.verify.first_error_path && <div className="mt-1 break-words text-rose-700">{state.verify.first_error_path}</div>}
        </div>
      )}
    </section>
  );
}

function Stat({ label, value, tone = "neutral" }: { label: string; value: string; tone?: "good" | "bad" | "neutral" }) {
  const valueClass = tone === "bad" ? "text-rose-700" : tone === "good" ? "text-emerald-700" : "text-slate-950";
  return (
    <div className="rounded-lg bg-slate-50 px-4 py-3">
      <div className="text-xs font-medium uppercase tracking-wide text-slate-500">{label}</div>
      <div className={`mt-1 text-lg font-semibold ${valueClass}`}>{value}</div>
    </div>
  );
}
