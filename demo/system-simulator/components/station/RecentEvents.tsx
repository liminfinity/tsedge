import type { LiveState } from "@/lib/schema";
import { stationEventText } from "./stationData";

export function RecentEvents({ state }: { state: LiveState }) {
  const events = state.events
    .filter((event) => ["append", "network", "pollution", "recovery", "export", "error"].includes(event.type))
    .slice(-8)
    .reverse();

  return (
    <section className="panel rounded-2xl p-5">
      <h2 className="text-lg font-semibold text-slate-950">Последние события</h2>
      <div className="mt-4 space-y-2">
        {events.map((event, index) => (
          <div key={`${event.time}-${index}`} className="flex gap-3 rounded-xl border border-slate-100 bg-white px-4 py-3">
            <span className="shrink-0 text-sm text-slate-500">{event.time}</span>
            <span className="text-sm font-medium text-slate-900">{stationEventText(event.message)}</span>
          </div>
        ))}
        {events.length === 0 && <div className="text-sm text-slate-500">Событий пока нет.</div>}
      </div>
    </section>
  );
}
