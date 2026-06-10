import { z } from "zod";

export const commandSchema = z.object({
  command: z.enum([
    "network_offline",
    "network_online",
    "simulate_pollution",
    "simulate_crash",
    "recover_from_wal",
    "append_one",
    "append_100",
    "append_1000",
    "append_batch",
    "flush_all",
    "read_last_range",
    "aggregate_avg",
    "aggregate_min_max",
    "run_retention",
    "export_csv",
    "verify_db",
    "create_debug_series",
    "delete_debug_series",
    "reset_demo",
    "pause",
    "resume"
  ]),
  series: z.string().optional()
});

export const liveStateSchema = z.object({
  running: z.boolean(),
  tick: z.number(),
  mode: z.string(),
  scenario: z.object({
    title: z.string(),
    short_description: z.string()
  }),
  network: z.object({
    online: z.boolean(),
    status: z.string()
  }),
  agent: z.object({
    status: z.string(),
    last_update_ms: z.number(),
    points_written_per_series: z.number(),
    total_points_written: z.number()
  }),
  last_batch: z.object({
    point_count: z.number(),
    timestamp: z.number(),
    series: z.array(
      z.object({
        name: z.string(),
        title: z.string(),
        value: z.number(),
        unit: z.string()
      })
    )
  }),
  wal: z.object({
    status: z.string(),
    last_write: z.boolean(),
    entries_since_flush: z.number(),
    replayed: z.boolean()
  }),
  buffer: z.object({
    points: z.number(),
    capacity: z.number(),
    percent: z.number()
  }),
  compression: z.object({
    active: z.boolean(),
    last_block_points: z.number(),
    last_block_created: z.boolean(),
    algorithm: z.string()
  }),
  storage: z
    .object({
      raw_size_estimate_bytes: z.number(),
      compressed_size_bytes: z.number(),
      compression_ratio: z.number(),
      bytes_per_point: z.number()
    })
    .default({
      raw_size_estimate_bytes: 0,
      compressed_size_bytes: 0,
      compression_ratio: 0,
      bytes_per_point: 0
    }),
  segments: z.array(
    z.object({
      name: z.string(),
      status: z.string(),
      blocks: z.number(),
      old: z.boolean(),
      deleted: z.boolean()
    })
  ),
  retention: z.object({
    enabled: z.boolean(),
    last_run: z.boolean(),
    last_deleted: z.string().nullable(),
    deleted_count: z.number()
  }),
  recovery: z.object({
    crash_simulated: z.boolean(),
    wal_replayed: z.boolean(),
    recovered_points: z.number()
  }),
  export: z
    .object({
      available: z.boolean().default(true),
      last_run: z.boolean().default(false),
      ok: z.boolean().default(false),
      csv_ready: z.boolean(),
      series: z.string().nullable().default(null),
      last_file: z.string().nullable(),
      path: z.string().nullable().default(null),
      rows: z.number().default(0),
      message: z.string().nullable().default(null)
    })
    .default({
      available: true,
      last_run: false,
      ok: false,
      csv_ready: false,
      series: null,
      last_file: null,
      path: null,
      rows: 0,
      message: null
    }),
  last_command: z
    .object({
      name: z.string().nullable(),
      status: z.string(),
      message: z.string(),
      affected_points: z.number(),
      timestamp: z.number()
    })
    .default({
      name: null,
      status: "not_run",
      message: "",
      affected_points: 0,
      timestamp: 0
    }),
  last_query: z
    .object({
      type: z.string().nullable(),
      series: z.string().nullable(),
      status: z.string(),
      from: z.number(),
      to: z.number(),
      points_read: z.number(),
      result: z.number(),
      min: z.number(),
      max: z.number(),
      duration_ms: z.number()
    })
    .default({
      type: null,
      series: null,
      status: "not_run",
      from: 0,
      to: 0,
      points_read: 0,
      result: 0,
      min: 0,
      max: 0,
      duration_ms: 0
    }),
  verify: z
    .object({
      last_run: z.boolean(),
      available: z.boolean().default(true),
      ok: z.boolean(),
      series_count: z.number(),
      segment_count: z.number(),
      block_count: z.number(),
      wal_entry_count: z.number(),
      error_count: z.number(),
      first_error_path: z.string().nullable(),
      first_error_message: z.string().nullable()
    })
    .default({
      last_run: false,
      available: true,
      ok: false,
      series_count: 0,
      segment_count: 0,
      block_count: 0,
      wal_entry_count: 0,
      error_count: 0,
      first_error_path: null,
      first_error_message: null
    }),
  series_list: z
    .object({
      status: z.string().default("ok"),
      message: z.string().optional(),
      count: z.number(),
      items: z.array(
        z.object({
          name: z.string(),
          total_points: z.number(),
          segment_count: z.number(),
          block_count: z.number(),
          compressed_size_bytes: z.number()
        })
      )
    })
    .default({
      status: "ok",
      count: 0,
      items: []
    }),
  events: z.array(
    z.object({
      time: z.string(),
      type: z.string(),
      message: z.string()
    })
  )
});

export type LiveState = z.infer<typeof liveStateSchema>;
export type DemoCommand = z.infer<typeof commandSchema>["command"];
