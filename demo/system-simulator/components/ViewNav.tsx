"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

const links = [
  { href: "/", label: "Панель метеопоста" },
  { href: "/diagnostics", label: "Диагностика TSEdge" }
];

export function ViewNav() {
  const pathname = usePathname();

  return (
    <nav className="inline-flex rounded-xl border border-slate-200 bg-white p-1 shadow-sm" aria-label="Переключение интерфейса">
      {links.map((link) => {
        const active = link.href === "/" ? pathname === "/" : pathname.startsWith(link.href);
        return (
          <Link
            key={link.href}
            href={link.href}
            className={`rounded-lg px-4 py-2 text-sm font-medium transition focus-visible:ring-2 focus-visible:ring-sky-400 ${
              active ? "bg-sky-600 text-white" : "text-slate-600 hover:bg-slate-100 hover:text-slate-950"
            }`}
          >
            {link.label}
          </Link>
        );
      })}
    </nav>
  );
}
