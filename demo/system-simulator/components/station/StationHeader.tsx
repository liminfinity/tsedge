import { Radio, Wifi, WifiOff } from "lucide-react";
import type { LucideIcon } from "lucide-react";
import { formatClock } from "@/lib/format";
import type { LiveState } from "@/lib/schema";
import { getStationStatus, toneClass } from "./stationData";
import type { Tone } from "./stationTypes";

export function StationHeader({
  state,
  values,
  lastReceivedAt,
  error
}: {
  state: LiveState;
  values: Record<string, number>;
  lastReceivedAt: number | null;
  error: string;
}) {
  const stationStatus = getStationStatus(state, values["pm25.concentration"] ?? 0, values["battery.voltage"] ?? 0);

  return (
    <header className="panel rounded-2xl p-6">
      <div className="flex flex-col gap-5 xl:flex-row xl:items-start xl:justify-between">
        <div>
          <h1 className="text-3xl font-semibold tracking-tight text-slate-950">Автономный метеопост</h1>
          <p className="mt-2 max-w-2xl text-base text-slate-600">
            Локальная панель станции. Данные приходят от C-приложения, которое пишет точки в TSEdge.
          </p>
        </div>
        <div className="grid gap-3 sm:grid-cols-2 xl:min-w-[520px]">
          <StatusCard label="Статус" value={stationStatus.label} tone={stationStatus.tone} icon={stationStatus.icon} />
          <StatusCard label="Связь" value={state.network.online ? "Связь есть" : "Связи нет"} tone={state.network.online ? "good" : "bad"} icon={state.network.online ? Wifi : WifiOff} />
          <StatusCard label="Режим" value={state.network.online ? "Онлайн" : "Автономно"} tone={state.network.online ? "good" : "warn"} icon={Radio} />
          <StatusCard label="Обновлено" value={lastReceivedAt ? formatClock(lastReceivedAt) : "нет данных"} />
        </div>
      </div>
      {error && (
        <div className="mt-4 rounded-lg border border-amber-200 bg-amber-50 px-4 py-2 text-sm text-amber-900">
          {error}. Последние данные оставлены на экране.
        </div>
      )}
    </header>
  );
}

function StatusCard({ label, value, icon: Icon, tone = "neutral" }: { label: string; value: string; icon?: LucideIcon; tone?: Tone }) {
  return (
    <div className="rounded-xl border border-slate-200 bg-white px-4 py-3">
      <div className="flex items-center gap-2 text-sm text-slate-500">
        {Icon && <Icon size={16} />}
        {label}
      </div>
      <div className={`mt-1 text-lg font-semibold ${toneClass(tone)}`}>{value}</div>
    </div>
  );
}
