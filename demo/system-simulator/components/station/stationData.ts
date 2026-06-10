import { AlertTriangle, Battery, Cloud, Droplets, Gauge, Radio, Thermometer, WifiOff, Wind } from "lucide-react";
import type { LucideIcon } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import type { StationSample, Tone } from "./stationTypes";

export const stationMetrics: Array<{
  name: string;
  title: string;
  unit: string;
  icon: LucideIcon;
  decimals: number;
}> = [
  { name: "air.temperature", title: "Температура", unit: "°C", icon: Thermometer, decimals: 1 },
  { name: "air.humidity", title: "Влажность", unit: "%", icon: Droplets, decimals: 0 },
  { name: "air.pressure", title: "Давление", unit: "гПа", icon: Gauge, decimals: 0 },
  { name: "wind.speed", title: "Ветер", unit: "м/с", icon: Wind, decimals: 1 },
  { name: "pm25.concentration", title: "PM2.5", unit: "мкг/м³", icon: Cloud, decimals: 1 },
  { name: "battery.voltage", title: "Аккумулятор", unit: "В", icon: Battery, decimals: 2 }
];

export function makeSample(state: LiveState): StationSample {
  return {
    totalPoints: state.agent.total_points_written,
    values: Object.fromEntries(state.last_batch.series.map((item) => [item.name, item.value]))
  };
}

export function getStationStatus(state: LiveState, pm25: number, battery: number): { label: string; tone: Tone; icon: LucideIcon } {
  if (state.agent.status === "crashed") {
    return { label: "Сбой", tone: "bad", icon: AlertTriangle };
  }
  if (pm25 > 75 || battery < 3.4) {
    return { label: "Предупреждение", tone: "warn", icon: AlertTriangle };
  }
  if (!state.network.online) {
    return { label: "Работает автономно", tone: "warn", icon: WifiOff };
  }
  return { label: "Станция работает", tone: "good", icon: Radio };
}

export function airQuality(pm25: number) {
  if (pm25 > 75) {
    return "Опасный PM2.5";
  }
  if (pm25 > 35) {
    return "Пыль повышена";
  }
  return "Воздух в норме";
}

export function weatherText(values: Record<string, number>) {
  const temp = values["air.temperature"] ?? 0;
  const wind = values["wind.speed"] ?? 0;
  if (wind > 8) {
    return "Ветрено";
  }
  if (temp > 25) {
    return "Тепло";
  }
  if (temp < 5) {
    return "Холодно";
  }
  return "Спокойно";
}

export function stationEventText(message: string) {
  if (message.includes("Получено 6 новых точек")) {
    return "Данные обновлены";
  }
  if (message.includes("Связь потеряна")) {
    return "Связь потеряна";
  }
  if (message.includes("Связь восстановлена")) {
    return "Связь восстановлена";
  }
  if (message.includes("PM2.5")) {
    return "Зафиксирован всплеск PM2.5";
  }
  if (message.includes("восстанов")) {
    return "Станция восстановлена";
  }
  if (message.includes("Сбой")) {
    return "Сбой питания";
  }
  if (message.includes("CSV")) {
    return "CSV готов";
  }
  return message;
}

export function toneClass(tone: Tone) {
  if (tone === "good") {
    return "text-emerald-700";
  }
  if (tone === "warn") {
    return "text-amber-700";
  }
  if (tone === "bad") {
    return "text-rose-700";
  }
  return "text-slate-950";
}
