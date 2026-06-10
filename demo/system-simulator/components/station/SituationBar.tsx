import { airQuality, toneClass, weatherText } from "./stationData";
import type { Tone } from "./stationTypes";

export function SituationBar({
  values,
  pm25
}: {
  values: Record<string, number>;
  pm25: number;
}) {
  return (
    <section className="panel rounded-2xl p-4">
      <div className="grid gap-3 md:grid-cols-2">
        <CompactStatus
          label="Воздух"
          value={airQuality(pm25)}
          detail={`${pm25.toFixed(1)} мкг/м³ PM2.5`}
          tone={pm25 > 75 ? "bad" : pm25 > 35 ? "warn" : "good"}
        />
        <CompactStatus
          label="Погода"
          value={weatherText(values)}
          detail={`${(values["wind.speed"] ?? 0).toFixed(1)} м/с, ${(values["air.temperature"] ?? 0).toFixed(1)} °C`}
          tone="neutral"
        />
      </div>
    </section>
  );
}

function CompactStatus({
  label,
  value,
  detail,
  tone
}: {
  label: string;
  value: string;
  detail: string;
  tone: Tone;
}) {
  return (
    <div className="flex items-center justify-between gap-4 rounded-xl bg-white px-4 py-3">
      <div>
        <div className="text-sm text-slate-500">{label}</div>
        <div className={`mt-1 text-lg font-semibold ${toneClass(tone)}`}>{value}</div>
      </div>
      <div className="max-w-[45%] text-right text-sm leading-5 text-slate-500">{detail}</div>
    </div>
  );
}
