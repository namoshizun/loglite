// LogLite API Client

/** Set via VITE_API_BASE_URL (.env / .env.production). Empty = same-origin (Nginx in prod). */
const API_BASE = (import.meta.env.VITE_API_BASE_URL ?? '').replace(/\/$/, '');

function apiUrl(path: string): string {
  const p = path.startsWith('/') ? path : `/${path}`;
  return API_BASE ? `${API_BASE}${p}` : p;
}

export interface HealthResponse {
  status: 'ok' | 'error';
}

export interface VersionResponse {
  version: string;
}

export interface SettingEntry {
  key: string;
  value: string | number | boolean | Record<string, string> | string[];
  description: string;
}

export interface SettingsResponse {
  settings: SettingEntry[];
}

export interface StatsResponse {
  activities: {
    fields: string[];
    data: any[][];
  };
  database: {
    fields: string[];
    data: any[][];
  };
  uptime: number;
}

export interface QueryFilter {
  field: string;
  op: '=' | '!=' | '>' | '>=' | '<' | '<=' | '~=';
  value: string;
}

export type LogColumnKind =
  | 'integer'
  | 'number'
  | 'text'
  | 'datetime'
  | 'json'
  | 'blob'
  | 'boolean';

export interface LogSchemaColumn {
  name: string;
  kind: LogColumnKind;
  sqlite_type: string;
  compressed: boolean;
  not_null: boolean;
  primary_key: boolean;
}

export interface LogSchemaResponse {
  table: string;
  columns: LogSchemaColumn[];
}

export interface QueryLogsParams {
  fields?: string;
  limit: number;
  offset: number;
  filters?: QueryFilter[];
}

export interface PaginatedLogs {
  limit: number;
  offset: number;
  total: number;
  results: Record<string, any>[];
}

// Convert activities or database stats matrices to array of objects for easier chart consumption
export interface ActivityStatRecord {
  since: string;
  until: string;
  query_count: number;
  query_min: number;
  query_max: number;
  query_avg: number;
  ingest_count: number;
  ingest_size_min: number;
  ingest_size_max: number;
  ingest_size_avg: number;
  ingest_drop_count: number;
  insert_batch_count: number;
  insert_total_count: number;
  insert_total_cost: number;
  sse_session_count: number;
  http_conn_count: number;
  [key: string]: any;
}

export interface DatabaseStatRecord {
  timestamp: string;
  rows_count: number;
  db_size: number;
  [key: string]: any;
}

export function transformStats(response: StatsResponse) {
  const activities = response.activities.data.map((row) => {
    const record: Record<string, any> = {};
    response.activities.fields.forEach((field, idx) => {
      record[field] = row[idx];
    });
    return record as ActivityStatRecord;
  });

  const database = response.database.data.map((row) => {
    const record: Record<string, any> = {};
    response.database.fields.forEach((field, idx) => {
      record[field] = row[idx];
    });
    return record as DatabaseStatRecord;
  });

  // Sort chronologically (oldest first) for charts
  activities.reverse();
  database.reverse();

  return { activities, database, uptime: response.uptime };
}

// Helper to serialize filters into query params
// Filters for the same key are merged into key=op1+val1,op2+val2
export function serializeQueryParams(params: QueryLogsParams): Record<string, string> {
  const query: Record<string, string> = {
    fields: params.fields || '*',
    limit: String(params.limit),
    offset: String(params.offset),
  };

  if (params.filters && params.filters.length > 0) {
    // Group filters by field name
    const grouped: Record<string, QueryFilter[]> = {};
    params.filters.forEach((f) => {
      if (!grouped[f.field]) {
        grouped[f.field] = [];
      }
      grouped[f.field].push(f);
    });

    // Format: field=op1val1,op2val2
    Object.entries(grouped).forEach(([field, filters]) => {
      const expr = filters.map((f) => `${f.op}${f.value}`).join(',');
      query[field] = expr;
    });
  }

  return query;
}

export async function fetchHealth(): Promise<HealthResponse> {
  const res = await fetch(apiUrl('/health'));
  if (!res.ok) {
    throw new Error('Health check failed');
  }
  return res.json();
}

export async function fetchSettings(): Promise<SettingsResponse> {
  const res = await fetch(apiUrl('/settings'));
  if (!res.ok) {
    const errorBody = await res.json().catch(() => ({}));
    throw new Error(errorBody.error || 'Failed to fetch settings');
  }
  return res.json();
}

export async function fetchVersion(): Promise<VersionResponse> {
  const res = await fetch(apiUrl('/version'));
  if (!res.ok) {
    throw new Error('Failed to fetch version');
  }
  return res.json();
}

export async function fetchStats(
  since: string,
  until: string,
): Promise<ReturnType<typeof transformStats>> {
  const params = new URLSearchParams({
    since,
    until,
    activity_stats_fields: '*',
    database_stats_fields: '*',
    ordering: 'desc', // Server limits 1 day, query descending, we will reverse it for chart
  });

  const res = await fetch(apiUrl(`/stats?${params.toString()}`));
  if (!res.ok) {
    const errorBody = await res.json().catch(() => ({}));
    throw new Error(errorBody.error || 'Failed to fetch stats');
  }
  const data: StatsResponse = await res.json();
  return transformStats(data);
}

export async function fetchLogSchema(): Promise<LogSchemaResponse> {
  const res = await fetch(apiUrl('/schema'));
  if (!res.ok) {
    const errorBody = await res.json().catch(() => ({}));
    throw new Error(errorBody.error || 'Failed to fetch log schema');
  }
  return res.json();
}

export async function fetchLogs(params: QueryLogsParams): Promise<PaginatedLogs> {
  const serialized = serializeQueryParams(params);
  const searchParams = new URLSearchParams();
  Object.entries(serialized).forEach(([key, val]) => {
    searchParams.append(key, val);
  });

  const res = await fetch(apiUrl(`/logs?${searchParams.toString()}`));
  if (!res.ok) {
    const errorBody = await res.json().catch(() => ({}));
    throw new Error(errorBody.error || 'Failed to fetch logs');
  }
  return res.json();
}

export async function postLog(body: Record<string, unknown>): Promise<{ status: string }> {
  const res = await fetch(apiUrl('/logs'), {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    const errorBody = await res.json().catch(() => ({}));
    throw new Error(errorBody.error || 'Failed to post log');
  }
  return res.json();
}

export function getSSEUrl(fields: string = '*'): string {
  return apiUrl(`/logs/sse?fields=${encodeURIComponent(fields)}`);
}
