import { useMemo } from "react";
import { ViewNav } from "@/components/ViewNav";
import { ExportCsvAction } from "./ExportCsvAction";
import { MetricCard } from "./MetricCard";
import { RecentEvents } from "./RecentEvents";
import { SituationBar } from "./SituationBar";
import { StationHeader } from "./StationHeader";
import { stationMetrics } from "./stationData";
import type { StationPanelProps } from "./stationTypes";

export function StationDashboard({
  state,
  history,
  lastReceivedAt,
  error,
  commandMessage,
  commandError,
  pendingCommand,
  onCommand
}: StationPanelProps) {
  const values = useMemo(() => Object.fromEntries(state.last_batch.series.map((item) => [item.name, item.value])), [state.last_batch.series]);
  const pm25 = values["pm25.concentration"] ?? 0;

  return (
    <main className="mx-auto max-w-[1500px] space-y-5 p-4 lg:p-6">
      <div className="flex justify-end">
        <ViewNav />
      </div>

      <StationHeader state={state} values={values} lastReceivedAt={lastReceivedAt} error={error} />

      <section className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
        {stationMetrics.map((metric) => (
          <MetricCard key={metric.name} metric={metric} value={values[metric.name]} history={history.map((sample) => sample.values[metric.name] ?? 0)} />
        ))}
      </section>

      <SituationBar values={values} pm25={pm25} />

      <section className="grid gap-4 lg:grid-cols-[0.9fr_1.1fr]">
        <RecentEvents state={state} />
        <div className="panel rounded-2xl p-5">
          <h2 className="text-lg font-semibold text-slate-950">Выгрузка данных</h2>
          <p className="mt-2 text-sm leading-6 text-slate-600">
            CSV нужен, чтобы забрать накопленные данные станции.
          </p>
          <ExportCsvAction
            state={state}
            pending={pendingCommand === "export_csv"}
            message={commandMessage}
            error={commandError}
            onExport={(series) => onCommand("export_csv", series)}
          />
        </div>
      </section>
    </main>
  );
}
