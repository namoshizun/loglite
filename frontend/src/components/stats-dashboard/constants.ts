import type { ActivityStatRecord } from '../../api/client';
import type { SubMetricsState, TimeRange } from './types';

export const CHART_GRID = 'var(--chart-grid)';
export const CHART_AXIS = 'var(--chart-axis)';

export const chartTooltipStyle = {
  backgroundColor: 'var(--chart-tooltip-bg)',
  borderColor: 'var(--chart-tooltip-border)',
  color: 'var(--chart-tooltip-fg)',
};

export const DEFAULT_SUB_METRICS: SubMetricsState = {
  query: { count: true, latency: false },
  ingestion: { count: true, size: false },
  insertion: { count: true, cost: false },
  connections: { http: true, sse: false },
};

const RANGE_MS: Record<TimeRange, number> = {
  '1h': 60 * 60 * 1000,
  '3h': 3 * 60 * 60 * 1000,
  '6h': 6 * 60 * 60 * 1000,
  '12h': 12 * 60 * 60 * 1000,
  '24h': 24 * 60 * 60 * 1000,
};

export function getStatsTimeWindow(range: TimeRange, now = new Date()) {
  const since = new Date(now.getTime() - RANGE_MS[range]);
  return { since: since.toISOString(), until: now.toISOString() };
}

export function enrichActivityRows(rows: ActivityStatRecord[]) {
  return rows.map((row) => ({
    ...row,
    query_latency_range: [row.query_min, row.query_max] as [number, number],
  }));
}

export type EnrichedActivityRow = ReturnType<typeof enrichActivityRows>[number];

export function formatChartTime(isoString: string): string {
  try {
    return new Date(isoString).toLocaleTimeString([], {
      hour: '2-digit',
      minute: '2-digit',
    });
  } catch {
    return isoString;
  }
}

export function formatChartTooltipLabel(value: unknown): string {
  return `Time: ${new Date(String(value)).toLocaleString()}`;
}
