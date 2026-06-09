export function formatNumber(value: number) {
  return new Intl.NumberFormat("ru-RU").format(value);
}

export function formatPercent(value: number) {
  return `${Math.max(0, Math.min(100, value)).toFixed(1)}%`;
}

export function formatTimestamp(value: number) {
  return new Date(value).toLocaleString("ru-RU");
}

export function formatClock(value: number) {
  return new Date(value).toLocaleTimeString("ru-RU", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  });
}

export function formatBytes(value: number) {
  if (value < 1024) {
    return `${formatNumber(Math.round(value))} Б`;
  }
  if (value < 1024 * 1024) {
    return `${(value / 1024).toFixed(1)} КБ`;
  }
  return `${(value / (1024 * 1024)).toFixed(1)} МБ`;
}
