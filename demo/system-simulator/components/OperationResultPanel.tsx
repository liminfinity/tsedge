import { CheckCircle2, Clock3, TerminalSquare, XCircle } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatNumber } from "@/lib/format";

const commandLabels: Record<string, string> = {
  append_one: "Добавить 1 точку",
  append_100: "Добавить 100",
  append_1000: "Добавить 1000",
  append_batch: "Batch-запись",
  flush_all: "Сброс буфера",
  read_last_range: "Прочитать диапазон",
  aggregate_avg: "AVG",
  aggregate_min_max: "MIN/MAX",
  run_retention: "Очистка старых данных",
  export_csv: "Выгрузка CSV",
  verify_db: "Проверка базы",
  create_debug_series: "Создать тестовый ряд",
  delete_debug_series: "Удалить тестовый ряд",
  simulate_crash: "Сбой",
  recover_from_wal: "WAL replay",
  pause: "Пауза",
  resume: "Продолжить",
  reset_demo: "Сброс demo",
  network_offline: "Потерять связь",
  network_online: "Восстановить связь",
  simulate_pollution: "Всплеск PM2.5"
};

export function OperationResultPanel({ state }: { state: LiveState }) {
  const commandName = state.last_command.name ? commandLabels[state.last_command.name] ?? state.last_command.name : "не запускалась";
  const commandOk = state.last_command.status === "ok";
  const query = state.last_query;
  const queryTitle = query.type ? formatQueryTitle(query.type, query.series) : "не запускался";

  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center gap-3">
        <TerminalSquare className="h-5 w-5 text-sky-700" />
        <h2 className="text-lg font-semibold">Результат операции</h2>
      </div>

      <div className="mt-5 grid gap-4 lg:grid-cols-2">
        <div className="rounded-lg bg-slate-50 p-4">
          <div className="flex items-center justify-between gap-3">
            <div>
              <div className="text-xs font-medium uppercase tracking-wide text-slate-500">Последняя команда</div>
              <div className="mt-1 font-semibold text-slate-950">{commandName}</div>
            </div>
            {state.last_command.status === "not_run" ? (
              <Clock3 className="h-5 w-5 text-slate-400" />
            ) : commandOk ? (
              <CheckCircle2 className="h-5 w-5 text-emerald-700" />
            ) : (
              <XCircle className="h-5 w-5 text-rose-700" />
            )}
          </div>
          <div className="mt-3 text-sm text-slate-600">{state.last_command.message || "Команд ещё не было."}</div>
          <div className="mt-3 grid grid-cols-2 gap-2 text-sm">
            <SmallStat label="Статус" value={state.last_command.status === "not_run" ? "нет" : commandOk ? "ok" : "ошибка"} />
            <SmallStat label="Точек" value={formatNumber(state.last_command.affected_points)} />
          </div>
        </div>

        <div className="rounded-lg bg-slate-50 p-4">
          <div className="text-xs font-medium uppercase tracking-wide text-slate-500">Последний запрос</div>
          <div className="mt-1 font-semibold text-slate-950">{queryTitle}</div>
          <div className="mt-3 grid grid-cols-2 gap-2 text-sm">
            <SmallStat label="Прочитано" value={formatNumber(query.points_read)} />
            <SmallStat label="Время" value={`${query.duration_ms.toFixed(2)} мс`} />
            {query.type === "AVG" && <SmallStat label="AVG" value={query.result.toFixed(2)} />}
            {query.type === "MIN_MAX" && <SmallStat label="MIN/MAX" value={`${query.min.toFixed(2)} / ${query.max.toFixed(2)}`} />}
            {query.type === "READ_RANGE" && <SmallStat label="Ряд" value={query.series ?? "-"} />}
          </div>
        </div>
      </div>
    </section>
  );
}

function SmallStat({ label, value }: { label: string; value: string }) {
  return (
    <div className="rounded-md bg-white px-3 py-2">
      <div className="text-xs text-slate-500">{label}</div>
      <div className="mt-0.5 break-words font-semibold text-slate-950">{value}</div>
    </div>
  );
}

function formatQueryTitle(type: string, series: string | null) {
  if (type === "READ_RANGE") {
    return `Диапазон ${series ?? ""}`.trim();
  }
  if (type === "MIN_MAX") {
    return `MIN/MAX ${series ?? ""}`.trim();
  }
  return `${type} ${series ?? ""}`.trim();
}
