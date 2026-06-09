import { Activity, Database, HardDrive, Radio, WifiOff } from "lucide-react";
import type { LucideIcon } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { formatClock, formatNumber } from "@/lib/format";

export function StatusHeader({
  state,
  live,
  lastReceivedAt,
  stale,
  onToggleLive
}: {
  state: LiveState;
  live: boolean;
  lastReceivedAt: number | null;
  stale: boolean;
  onToggleLive: () => void;
}) {
  const crashed = state.agent.status === "crashed";
  const activeSegment = state.segments.find((segment) => segment.status === "active")?.name ?? "нет";
  const agentLabel =
    crashed ? "Сбой" : state.agent.status === "paused" ? "Пауза" : state.agent.status === "stopped" ? "Остановлен" : "Работает";
  return (
    <header className="panel rounded-xl p-5">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
        <div>
          <h1 className="text-2xl font-semibold text-slate-950">Диагностика TSEdge</h1>
          <p className="mt-1 text-sm text-slate-500">Локальная панель базы</p>
        </div>
        <button
          onClick={onToggleLive}
          className={`min-h-10 rounded-lg px-4 py-2 text-sm font-medium outline-none transition focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 ${live ? "bg-sky-600 text-white hover:bg-sky-700" : "bg-slate-200 text-slate-800 hover:bg-slate-300"}`}
        >
          Автообновление: {live ? "вкл" : "выкл"}
        </button>
      </div>
      <div className="mt-5 grid gap-3 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-6">
        <Status label="Агент" value={agentLabel} icon={Activity} tone={crashed ? "bad" : state.agent.status === "paused" || state.agent.status === "stopped" ? "warn" : "good"} />
        <Status label="Связь" value={state.network.online ? "Связь есть" : "Связи нет"} icon={state.network.online ? Radio : WifiOff} tone={state.network.online ? "good" : "bad"} />
        <Status label="Обновлено" value={lastReceivedAt ? formatClock(lastReceivedAt) : "нет данных"} tone={stale ? "warn" : undefined} />
        <Status label="Точек" value={formatNumber(state.agent.total_points_written)} />
        <Status label="Активный segment" value={activeSegment} icon={HardDrive} />
        <Status label="Segment-файлов" value={formatNumber(state.segments.length)} icon={Database} />
      </div>
      {stale && (
        <div className="mt-4 rounded-lg border border-amber-200 bg-amber-50 px-4 py-2 text-sm text-amber-900">
          Данные не обновлялись больше 5 секунд.
        </div>
      )}
      {crashed && (
        <div className="mt-4 rounded-lg border border-rose-200 bg-rose-50 px-4 py-2 text-sm text-rose-900">
          Сбой смоделирован. Нажмите «Восстановить из WAL».
        </div>
      )}
    </header>
  );
}

function Status({ label, value, icon: Icon, tone }: { label: string; value: string; icon?: LucideIcon; tone?: "good" | "bad" | "warn" }) {
  return (
    <div className="min-w-0 rounded-lg border border-slate-200 bg-white px-4 py-3">
      <div className="flex items-center gap-2 text-xs text-slate-500">
        {Icon && <Icon size={15} />}
        {label}
      </div>
      <div className={`mt-1 truncate text-base font-semibold ${tone === "bad" ? "text-rose-700" : tone === "good" ? "text-emerald-700" : tone === "warn" ? "text-amber-700" : "text-slate-950"}`}>{value}</div>
    </div>
  );
}
