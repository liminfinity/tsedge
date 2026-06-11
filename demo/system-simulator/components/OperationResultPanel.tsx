import { CheckCircle2, Clock3, TerminalSquare, XCircle } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatClock, formatNumber } from "@/lib/format";

const commandLabels: Record<string, string> = {
  append_one: "Добавить 1 точку",
  append_100: "Добавить 100",
  append_1000: "Добавить 1000",
  append_custom: "Добавить точки",
  append_batch: "Batch-запись",
  flush_all: "Сброс буфера",
  read_last_range: "Прочитать диапазон",
  aggregate_avg: "AVG",
  aggregate_min_max: "MIN/MAX",
  window_aggregate: "Агрегация по окнам",
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

      <div className="mt-5 grid items-start gap-4 lg:grid-cols-2">
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
            <SmallStat label={query.type === "WINDOW" ? "Исходных точек" : "Прочитано"} value={formatNumber(query.points_read)} />
            <SmallStat label="Время" value={`${query.duration_ms.toFixed(2)} мс`} />
            {query.type === "AVG" && <SmallStat label="AVG" value={query.result.toFixed(2)} />}
            {query.type === "MIN_MAX" && <SmallStat label="MIN/MAX" value={`${query.min.toFixed(2)} / ${query.max.toFixed(2)}`} />}
            {query.type === "WINDOW" && <SmallStat label="Ряд" value={state.window_aggregate.series ?? "-"} />}
            {query.type === "WINDOW" && <SmallStat label="Окно, мс" value={formatNumber(state.window_aggregate.window_size)} />}
            {query.type === "WINDOW" && (
              <SmallStat
                label="Для графика"
                value={`${formatNumber(state.window_aggregate.source_points_estimate)} -> ${formatNumber(state.window_aggregate.window_count)} окон`}
              />
            )}
            {query.type === "WINDOW" && <SmallStat label="Сокращение" value={`${state.window_aggregate.downsample_ratio.toFixed(1)}x`} />}
            {query.type === "WINDOW" && <SmallStat label="MIN/MAX" value={`${state.window_aggregate.global_min.toFixed(2)} / ${state.window_aggregate.global_max.toFixed(2)}`} />}
            {query.type === "WINDOW" && <SmallStat label="AVG" value={state.window_aggregate.weighted_avg.toFixed(2)} />}
            {query.type === "WINDOW" && <SmallStat label="Время, с" value={state.window_aggregate.query_seconds.toFixed(4)} />}
            {query.type === "READ_RANGE" && <SmallStat label="Ряд" value={query.series ?? "-"} />}
          </div>
          {query.type === "WINDOW" && state.window_aggregate.first && state.window_aggregate.last && (
            <div className="mt-3 grid gap-2 text-sm xl:grid-cols-2">
              <WindowPreview label="Первое окно" window={state.window_aggregate.first} />
              <WindowPreview label="Последнее окно" window={state.window_aggregate.last} />
            </div>
          )}
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
  if (type === "WINDOW") {
    return `Окна ${series ?? ""}`.trim();
  }
  return `${type} ${series ?? ""}`.trim();
}

function WindowPreview({
  label,
  window
}: {
  label: string;
  window: { start: number; end: number; count: number; min: number; max: number; avg: number };
}) {
  return (
    <div className="rounded-md bg-white px-3 py-2">
      <div className="text-xs text-slate-500">{label}</div>
      <div className="mt-1 text-sm font-semibold text-slate-950">
        {formatClock(window.start)} - {formatClock(window.end)}
      </div>
      <div className="mt-2 grid grid-cols-2 gap-2 text-xs text-slate-600">
        <span>
          <span className="block text-slate-400">Точек</span>
          <span className="font-medium text-slate-800">{formatNumber(window.count)}</span>
        </span>
        <span>
          <span className="block text-slate-400">AVG</span>
          <span className="font-medium text-slate-800">{window.avg.toFixed(2)}</span>
        </span>
        <span className="col-span-2">
          <span className="block text-slate-400">MIN/MAX</span>
          <span className="font-medium text-slate-800">
            {window.min.toFixed(2)} / {window.max.toFixed(2)}
          </span>
        </span>
      </div>
    </div>
  );
}
