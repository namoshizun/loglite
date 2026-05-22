export type TimeRange = '1h' | '3h' | '6h' | '12h' | '24h';
export type ViewMode = 'activity' | 'database';
export type ActivityCategory = 'query' | 'ingestion' | 'insertion' | 'connections';

export type QuerySub = 'count' | 'latency';
export type IngestionSub = 'count' | 'size';
export type InsertionSub = 'count' | 'cost';
export type ConnectionsSub = 'http' | 'sse';

export type SubMetricsState = {
  query: Record<QuerySub, boolean>;
  ingestion: Record<IngestionSub, boolean>;
  insertion: Record<InsertionSub, boolean>;
  connections: Record<ConnectionsSub, boolean>;
};

export type ActivityCategoryConfig = {
  id: ActivityCategory;
  label: string;
  subs: { id: string; label: string }[];
};

export const TIME_RANGES: TimeRange[] = ['1h', '3h', '6h', '12h', '24h'];
