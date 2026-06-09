import type { LucideIcon } from "lucide-react";

type Props = {
  icon: LucideIcon;
  title: string;
  status: "active" | "wait" | "crash" | "unavailable";
};

const styles = {
  active: {
    card: "border-sky-300 bg-sky-50",
    icon: "bg-sky-100 text-sky-700",
    label: "активен",
    badge: "bg-sky-100 text-sky-800"
  },
  wait: {
    card: "border-slate-200 bg-white",
    icon: "bg-slate-100 text-slate-700",
    label: "ожидает",
    badge: "bg-slate-100 text-slate-700"
  },
  crash: {
    card: "border-rose-300 bg-rose-50",
    icon: "bg-rose-100 text-rose-700",
    label: "сбой",
    badge: "bg-rose-100 text-rose-800"
  },
  unavailable: {
    card: "border-slate-200 bg-slate-50",
    icon: "bg-slate-200 text-slate-500",
    label: "стоп",
    badge: "bg-slate-200 text-slate-600"
  }
} satisfies Record<Props["status"], { card: string; icon: string; label: string; badge: string }>;

export function PipelineNode({ icon: Icon, title, status }: Props) {
  const style = styles[status];
  return (
    <div className={`h-full rounded-lg border px-4 py-3 ${style.card}`}>
      <div className="flex min-w-0 items-center gap-3">
        <div className={`shrink-0 rounded-lg p-2.5 ${style.icon}`}>
          <Icon size={20} />
        </div>
        <div className="min-w-0 flex-1">
          <div className="truncate text-base font-semibold leading-6 text-slate-950">{title}</div>
          <div className={`mt-1 inline-flex rounded-md px-2 py-0.5 text-xs font-medium leading-5 ${style.badge}`}>{style.label}</div>
        </div>
      </div>
    </div>
  );
}
