import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { fetchStats } from '../api/client';
import {
  AreaChart,
  Area,
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from 'recharts';
import { Calendar, BarChart3, LineChart as ChartIcon, CheckSquare, Square } from 'lucide-react';

type TimeRange = '1h' | '3h' | '6h' | '12h' | '24h';

export default function StatsDashboard() {
  const [timeRange, setTimeRange] = useState<TimeRange>('6h');
  const [selectedChartType, setSelectedChartType] = useState<'activity' | 'database'>('activity');

  // Multi-select for activity metrics
  const [selectedMetrics, setSelectedMetrics] = useState<Record<string, boolean>>({
    ingest_count: true,
    query_count: false,
    query_avg: false,
    sse_session_count: true,
    http_conn_count: false,
    ingest_drop_count: false,
  });

  // Calculate since/until timestamps based on range
  const now = new Date();
  const getSinceDate = (range: TimeRange) => {
    const msMap: Record<TimeRange, number> = {
      '1h': 60 * 60 * 1000,
      '3h': 3 * 60 * 60 * 1000,
      '6h': 6 * 60 * 60 * 1000,
      '12h': 12 * 60 * 60 * 1000,
      '24h': 24 * 60 * 60 * 1000,
    };
    return new Date(now.getTime() - msMap[range]);
  };

  const since = getSinceDate(timeRange).toISOString();
  const until = now.toISOString();

  // Query Stats
  const { data: stats, isLoading, isError, error } = useQuery({
    queryKey: ['stats', timeRange],
    queryFn: () => fetchStats(since, until),
    refetchInterval: 15000, // Refresh every 15 seconds
  });

  const formatTime = (isoString: string) => {
    try {
      const date = new Date(isoString);
      return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    } catch {
      return isoString;
    }
  };

  const toggleMetric = (key: string) => {
    setSelectedMetrics((prev) => ({
      ...prev,
      [key]: !prev[key],
    }));
  };

  const metricColors: Record<string, string> = {
    ingest_count: '#3b82f6', // blue
    query_count: '#10b981', // green
    query_avg: '#f59e0b', // amber
    sse_session_count: '#8b5cf6', // purple
    http_conn_count: '#ec4899', // pink
    ingest_drop_count: '#ef4444', // red
    insert_total_count: '#14b8a6', // teal
  };

  const metricLabels: Record<string, string> = {
    ingest_count: 'Ingested Logs',
    query_count: 'Read Queries',
    query_avg: 'Avg Query Latency (ms)',
    sse_session_count: 'SSE Clients',
    http_conn_count: 'HTTP Connections',
    ingest_drop_count: 'Dropped Logs',
    insert_total_count: 'DB Inserts',
  };

  const renderActiveCharts = () => {
    if (isLoading) {
      return (
        <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
          <div className="flex flex-col items-center gap-2">
            <div className="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin"></div>
            <span>Loading database stats...</span>
          </div>
        </div>
      );
    }

    if (isError) {
      return (
        <div className="h-[300px] flex items-center justify-center text-destructive bg-destructive/5 border border-destructive/20 rounded-lg p-6 text-center">
          <div>
            <p className="font-semibold">Failed to fetch stats</p>
            <p className="text-xs text-muted-foreground mt-1">{(error as any)?.message || 'Unknown error'}</p>
          </div>
        </div>
      );
    }

    const hasActivityData = stats?.activities && stats.activities.length > 0;
    const hasDatabaseData = stats?.database && stats.database.length > 0;

    if (selectedChartType === 'activity') {
      if (!hasActivityData) {
        return (
          <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
            No activity logs recorded in this period.
          </div>
        );
      }

      // Check if at least one metric is selected
      const activeKeys = Object.entries(selectedMetrics)
        .filter(([_, enabled]) => enabled)
        .map(([key]) => key);

      if (activeKeys.length === 0) {
        return (
          <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
            Select one or more metrics on the right to visualize.
          </div>
        );
      }

      return (
        <div className="h-[300px] w-full">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={stats.activities} margin={{ top: 10, right: 10, left: -20, bottom: 0 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
              <XAxis dataKey="until" tickFormatter={formatTime} stroke="#71717a" fontSize={11} />
              <YAxis stroke="#71717a" fontSize={11} />
              <Tooltip
                contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#fafafa' }}
                labelFormatter={(value) => `Time: ${new Date(value).toLocaleString()}`}
              />
              <Legend verticalAlign="top" height={36} />
              {activeKeys.map((key) => (
                <Line
                  key={key}
                  type="monotone"
                  dataKey={key}
                  name={metricLabels[key] || key}
                  stroke={metricColors[key] || '#ffffff'}
                  strokeWidth={2}
                  dot={false}
                  activeDot={{ r: 4 }}
                />
              ))}
            </LineChart>
          </ResponsiveContainer>
        </div>
      );
    } else {
      if (!hasDatabaseData) {
        return (
          <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
            No database stats recorded in this period.
          </div>
        );
      }

      // Convert DB size in stats array to MB for readability
      const formattedDbStats = stats.database.map((d) => ({
        ...d,
        db_size_mb: Number((d.db_size / (1024 * 1024)).toFixed(2)),
      }));

      return (
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
          <div className="h-[280px] bg-zinc-900/40 p-3 rounded-lg border border-border">
            <h4 className="text-xs text-muted-foreground font-semibold mb-2 flex items-center gap-1.5">
              <ChartIcon size={12} className="text-blue-400" /> Database Size (MB)
            </h4>
            <ResponsiveContainer width="100%" height="90%">
              <AreaChart data={formattedDbStats} margin={{ top: 10, right: 5, left: -20, bottom: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
                <XAxis dataKey="timestamp" tickFormatter={formatTime} stroke="#71717a" fontSize={10} />
                <YAxis stroke="#71717a" fontSize={10} />
                <Tooltip
                  contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#fafafa' }}
                  labelFormatter={(value) => `Time: ${new Date(value).toLocaleString()}`}
                  formatter={(value) => [`${value} MB`, 'Database Size']}
                />
                <Area type="monotone" dataKey="db_size_mb" stroke="#3b82f6" fill="#3b82f6" fillOpacity={0.1} strokeWidth={2} />
              </AreaChart>
            </ResponsiveContainer>
          </div>

          <div className="h-[280px] bg-zinc-900/40 p-3 rounded-lg border border-border">
            <h4 className="text-xs text-muted-foreground font-semibold mb-2 flex items-center gap-1.5">
              <BarChart3 size={12} className="text-purple-400" /> Total Stored Rows
            </h4>
            <ResponsiveContainer width="100%" height="90%">
              <AreaChart data={formattedDbStats} margin={{ top: 10, right: 5, left: -20, bottom: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
                <XAxis dataKey="timestamp" tickFormatter={formatTime} stroke="#71717a" fontSize={10} />
                <YAxis stroke="#71717a" fontSize={10} />
                <Tooltip
                  contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#fafafa' }}
                  labelFormatter={(value) => `Time: ${new Date(value).toLocaleString()}`}
                  formatter={(value: any) => [value.toLocaleString(), 'Row Count']}
                />
                <Area type="monotone" dataKey="rows_count" stroke="#8b5cf6" fill="#8b5cf6" fillOpacity={0.1} strokeWidth={2} />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </div>
      );
    }
  };

  return (
    <div className="bg-card border border-border rounded-xl p-5 shadow-sm">
      {/* Selector Toolbar */}
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 mb-6 pb-4 border-b border-border">
        {/* Toggle Charts type */}
        <div className="flex bg-zinc-900 p-1 rounded-lg border border-border">
          <button
            onClick={() => setSelectedChartType('activity')}
            className={`px-3.5 py-1.5 text-xs font-semibold rounded-md transition-all duration-200 ${
              selectedChartType === 'activity'
                ? 'bg-zinc-800 text-foreground shadow-sm'
                : 'text-muted-foreground hover:text-foreground'
            }`}
          >
            System Activity
          </button>
          <button
            onClick={() => setSelectedChartType('database')}
            className={`px-3.5 py-1.5 text-xs font-semibold rounded-md transition-all duration-200 ${
              selectedChartType === 'database'
                ? 'bg-zinc-800 text-foreground shadow-sm'
                : 'text-muted-foreground hover:text-foreground'
            }`}
          >
            Database Growth
          </button>
        </div>

        {/* Time Window Buttons */}
        <div className="flex items-center gap-2">
          <Calendar size={14} className="text-muted-foreground" />
          <span className="text-xs text-muted-foreground mr-1">Time Range:</span>
          <div className="flex bg-zinc-900 p-1 rounded-lg border border-border">
            {(['1h', '3h', '6h', '12h', '24h'] as TimeRange[]).map((range) => (
              <button
                key={range}
                onClick={() => setTimeRange(range)}
                className={`px-2.5 py-1 text-xs font-mono rounded-md transition-all duration-200 ${
                  timeRange === range
                    ? 'bg-zinc-800 text-foreground shadow-sm font-bold'
                    : 'text-muted-foreground hover:text-foreground'
                }`}
              >
                {range}
              </button>
            ))}
          </div>
        </div>
      </div>

      {/* Main Grid: Chart + Sidebar (Metric Selector) */}
      <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
        <div className="lg:col-span-9">{renderActiveCharts()}</div>

        {/* Selected metric panel */}
        <div className="lg:col-span-3 lg:border-l lg:border-border lg:pl-6 flex flex-col justify-start">
          <h4 className="text-xs font-semibold text-muted-foreground uppercase tracking-wider mb-3">
            {selectedChartType === 'activity' ? 'Visible Metrics' : 'Database Status'}
          </h4>

          {selectedChartType === 'activity' ? (
            <div className="flex flex-col gap-2">
              {Object.entries(selectedMetrics).map(([key, value]) => (
                <button
                  key={key}
                  onClick={() => toggleMetric(key)}
                  className="flex items-center gap-2.5 px-2.5 py-1.5 rounded text-left text-xs text-foreground/90 hover:bg-zinc-900/60 transition-colors"
                >
                  {value ? (
                    <CheckSquare size={14} className="text-primary" />
                  ) : (
                    <Square size={14} className="text-zinc-600" />
                  )}
                  <span className="flex-1 font-medium">{metricLabels[key] || key}</span>
                  <span
                    className="w-2 h-2 rounded-full"
                    style={{ backgroundColor: metricColors[key] || '#fff' }}
                  />
                </button>
              ))}
            </div>
          ) : (
            <div className="text-xs text-muted-foreground space-y-2 bg-zinc-950/40 p-3 rounded-lg border border-border/60">
              <p>
                Database size and row counts are gathered automatically in the background by the LogLite diagnostics engine.
              </p>
              <p className="text-zinc-500 mt-1">
                Vaccuuming triggers automatically or on a schedule to recycle unused SQLite database pages.
              </p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
