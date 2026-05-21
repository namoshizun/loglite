import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { fetchStats } from "../api/client";
import type { ActivityStatRecord } from "../api/client";
import {
  Area,
  AreaChart,
  Bar,
  CartesianGrid,
  ComposedChart,
  Legend,
  Line,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import { BarChart3, Calendar, LineChart as ChartIcon } from "lucide-react";

type TimeRange = "1h" | "3h" | "6h" | "12h" | "24h";
type ViewMode = "activity" | "database";
type ActivityCategory = "query" | "ingestion" | "insertion" | "connections";

type QuerySub = "count" | "latency";
type IngestionSub = "count" | "size";
type InsertionSub = "count" | "cost";
type ConnectionsSub = "http" | "sse";

type SubMetricsState = {
  query: Record<QuerySub, boolean>;
  ingestion: Record<IngestionSub, boolean>;
  insertion: Record<InsertionSub, boolean>;
  connections: Record<ConnectionsSub, boolean>;
};

const DEFAULT_SUB_METRICS: SubMetricsState = {
  query: { count: true, latency: false },
  ingestion: { count: true, size: false },
  insertion: { count: true, cost: false },
  connections: { http: true, sse: false },
};

const ACTIVITY_CATEGORIES: {
  id: ActivityCategory;
  label: string;
  subs: { id: string; label: string }[];
}[] = [
  {
    id: "query",
    label: "Query",
    subs: [
      { id: "count", label: "Request count" },
      { id: "latency", label: "Processing latency" },
    ],
  },
  {
    id: "ingestion",
    label: "Ingestion",
    subs: [
      { id: "count", label: "Request count" },
      { id: "size", label: "Payload size" },
    ],
  },
  {
    id: "insertion",
    label: "Insertion",
    subs: [
      { id: "count", label: "Log count" },
      { id: "cost", label: "Time cost" },
    ],
  },
  {
    id: "connections",
    label: "Connections",
    subs: [
      { id: "http", label: "HTTP" },
      { id: "sse", label: "SSE" },
    ],
  },
];

const tooltipStyle = {
  backgroundColor: "#18181b",
  borderColor: "#27272a",
  color: "#fafafa",
};

function enrichActivityRows(rows: ActivityStatRecord[]) {
  return rows.map((row) => ({
    ...row,
    query_latency_range: [row.query_min, row.query_max] as [number, number],
  }));
}

export default function StatsDashboard() {
  const [timeRange, setTimeRange] = useState<TimeRange>("6h");
  const [viewMode, setViewMode] = useState<ViewMode>("activity");
  const [activityCategory, setActivityCategory] =
    useState<ActivityCategory>("query");
  const [subMetrics, setSubMetrics] =
    useState<SubMetricsState>(DEFAULT_SUB_METRICS);

  const toggleSubMetric = <C extends ActivityCategory>(
    category: C,
    subId: keyof SubMetricsState[C],
  ) => {
    setSubMetrics((prev) => ({
      ...prev,
      [category]: {
        ...prev[category],
        [subId]: !prev[category][subId],
      },
    }));
  };

  const enabledSubsForCategory = subMetrics[activityCategory];
  const hasEnabledSub = Object.values(enabledSubsForCategory).some(Boolean);

  const now = new Date();
  const getSinceDate = (range: TimeRange) => {
    const msMap: Record<TimeRange, number> = {
      "1h": 60 * 60 * 1000,
      "3h": 3 * 60 * 60 * 1000,
      "6h": 6 * 60 * 60 * 1000,
      "12h": 12 * 60 * 60 * 1000,
      "24h": 24 * 60 * 60 * 1000,
    };
    return new Date(now.getTime() - msMap[range]);
  };

  const since = getSinceDate(timeRange).toISOString();
  const until = now.toISOString();

  const {
    data: stats,
    isLoading,
    isError,
    error,
  } = useQuery({
    queryKey: ["stats", timeRange],
    queryFn: () => fetchStats(since, until),
    refetchInterval: 15000,
  });

  const formatTime = (isoString: string) => {
    try {
      return new Date(isoString).toLocaleTimeString([], {
        hour: "2-digit",
        minute: "2-digit",
      });
    } catch {
      return isoString;
    }
  };

  const renderActivityChart = (
    chartData: ReturnType<typeof enrichActivityRows>,
  ) => {
    if (!hasEnabledSub) {
      return null;
    }

    const common = (
      <>
        <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
        <XAxis
          dataKey="until"
          tickFormatter={formatTime}
          stroke="#71717a"
          fontSize={11}
        />
        <Tooltip
          contentStyle={tooltipStyle}
          labelFormatter={(value) =>
            `Time: ${new Date(String(value)).toLocaleString()}`
          }
        />
        <Legend verticalAlign="top" height={36} />
      </>
    );

    switch (activityCategory) {
      case "query": {
        const q = subMetrics.query;
        const needsLeft = q.count;
        const needsRight = q.latency;
        const marginRight = needsRight ? 12 : 10;
        return (
          <ComposedChart
            data={chartData}
            margin={{ top: 10, right: marginRight, left: -12, bottom: 0 }}
          >
            {common}
            {needsLeft && (
              <YAxis yAxisId="left" stroke="#71717a" fontSize={11} />
            )}
            {needsRight && (
              <YAxis
                yAxisId="right"
                orientation="right"
                stroke="#71717a"
                fontSize={11}
              />
            )}
            {q.count && (
              <Bar
                yAxisId="left"
                dataKey="query_count"
                name="Query count"
                fill="#10b981"
                fillOpacity={0.85}
                radius={[2, 2, 0, 0]}
              />
            )}
            {q.latency && (
              <>
                <Area
                  yAxisId={needsRight ? "right" : "left"}
                  dataKey="query_latency_range"
                  name="Latency (min–max)"
                  fill="#f59e0b"
                  fillOpacity={0.22}
                  stroke="none"
                  activeDot={false}
                />
                <Line
                  yAxisId={needsRight ? "right" : "left"}
                  type="monotone"
                  dataKey="query_avg"
                  name="Avg latency (ms)"
                  stroke="#f59e0b"
                  strokeWidth={2}
                  dot={false}
                  activeDot={{ r: 4 }}
                />
              </>
            )}
          </ComposedChart>
        );
      }
      case "ingestion": {
        const ing = subMetrics.ingestion;
        const needsLeft = ing.count;
        const needsRight = ing.size;
        const marginRight = needsRight ? 12 : 10;
        return (
          <ComposedChart
            data={chartData}
            margin={{ top: 10, right: marginRight, left: -12, bottom: 0 }}
          >
            {common}
            {needsLeft && (
              <YAxis yAxisId="left" stroke="#71717a" fontSize={11} />
            )}
            {needsRight && (
              <YAxis
                yAxisId="right"
                orientation="right"
                stroke="#71717a"
                fontSize={11}
              />
            )}
            {ing.count && (
              <Bar
                yAxisId="left"
                dataKey="ingest_count"
                name="Ingest count"
                fill="#3b82f6"
                fillOpacity={0.85}
                radius={[2, 2, 0, 0]}
              />
            )}
            {ing.size && (
              <Line
                yAxisId={needsRight ? "right" : "left"}
                type="monotone"
                dataKey="ingest_size_avg"
                name="Avg body size (bytes)"
                stroke="#60a5fa"
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 4 }}
              />
            )}
          </ComposedChart>
        );
      }
      case "insertion": {
        const ins = subMetrics.insertion;
        const needsLeft = ins.count;
        const needsRight = ins.cost;
        const marginRight = needsRight ? 12 : 10;
        return (
          <ComposedChart
            data={chartData}
            margin={{ top: 10, right: marginRight, left: -12, bottom: 0 }}
          >
            {common}
            {needsLeft && (
              <YAxis yAxisId="left" stroke="#71717a" fontSize={11} />
            )}
            {needsRight && (
              <YAxis
                yAxisId="right"
                orientation="right"
                stroke="#71717a"
                fontSize={11}
              />
            )}
            {ins.count && (
              <Bar
                yAxisId="left"
                dataKey="insert_total_count"
                name="Rows inserted"
                fill="#14b8a6"
                fillOpacity={0.85}
                radius={[2, 2, 0, 0]}
              />
            )}
            {ins.cost && (
              <Line
                yAxisId={needsRight ? "right" : "left"}
                type="monotone"
                dataKey="insert_total_cost"
                name="Insert cost (ms)"
                stroke="#2dd4bf"
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 4 }}
              />
            )}
          </ComposedChart>
        );
      }
      case "connections": {
        const conn = subMetrics.connections;
        return (
          <ComposedChart
            data={chartData}
            margin={{ top: 10, right: 10, left: -12, bottom: 0 }}
          >
            {common}
            <YAxis yAxisId="left" stroke="#71717a" fontSize={11} />
            {conn.http && (
              <Line
                yAxisId="left"
                type="monotone"
                dataKey="http_conn_count"
                name="HTTP connections"
                stroke="#ec4899"
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 4 }}
              />
            )}
            {conn.sse && (
              <Line
                yAxisId="left"
                type="monotone"
                dataKey="sse_session_count"
                name="SSE sessions"
                stroke="#8b5cf6"
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 4 }}
              />
            )}
          </ComposedChart>
        );
      }
    }
  };

  const renderActiveCharts = () => {
    if (isLoading) {
      return (
        <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
          <div className="flex flex-col items-center gap-2">
            <div className="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin" />
            <span>Loading stats...</span>
          </div>
        </div>
      );
    }

    if (isError) {
      return (
        <div className="h-[300px] flex items-center justify-center text-destructive bg-destructive/5 border border-destructive/20 rounded-lg p-6 text-center">
          <div>
            <p className="font-semibold">Failed to fetch stats</p>
            <p className="text-xs text-muted-foreground mt-1">
              {(error as Error)?.message || "Unknown error"}
            </p>
          </div>
        </div>
      );
    }

    if (viewMode === "activity") {
      if (!stats?.activities?.length) {
        return (
          <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
            No activity stats recorded in this period.
          </div>
        );
      }

      const chartData = enrichActivityRows(stats.activities);
      const chart = renderActivityChart(chartData);
      return (
        <div className="h-[300px] w-full">
          {chart ? (
            <ResponsiveContainer width="100%" height="100%">
              {chart}
            </ResponsiveContainer>
          ) : (
            <div className="h-full flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
              Select at least one metric below.
            </div>
          )}
        </div>
      );
    }

    if (!stats?.database?.length) {
      return (
        <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-zinc-950/20 border border-border rounded-lg">
          No database stats recorded in this period.
        </div>
      );
    }

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
            <AreaChart
              data={formattedDbStats}
              margin={{ top: 10, right: 5, left: -20, bottom: 0 }}
            >
              <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
              <XAxis
                dataKey="timestamp"
                tickFormatter={formatTime}
                stroke="#71717a"
                fontSize={10}
              />
              <YAxis stroke="#71717a" fontSize={10} />
              <Tooltip
                contentStyle={tooltipStyle}
                labelFormatter={(value) =>
                  `Time: ${new Date(String(value)).toLocaleString()}`
                }
                formatter={(value) => [`${value} MB`, "Database Size"]}
              />
              <Area
                type="monotone"
                dataKey="db_size_mb"
                stroke="#3b82f6"
                fill="#3b82f6"
                fillOpacity={0.1}
                strokeWidth={2}
              />
            </AreaChart>
          </ResponsiveContainer>
        </div>

        <div className="h-[280px] bg-zinc-900/40 p-3 rounded-lg border border-border">
          <h4 className="text-xs text-muted-foreground font-semibold mb-2 flex items-center gap-1.5">
            <BarChart3 size={12} className="text-purple-400" /> Total Stored
            Rows
          </h4>
          <ResponsiveContainer width="100%" height="90%">
            <AreaChart
              data={formattedDbStats}
              margin={{ top: 10, right: 5, left: -20, bottom: 0 }}
            >
              <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
              <XAxis
                dataKey="timestamp"
                tickFormatter={formatTime}
                stroke="#71717a"
                fontSize={10}
              />
              <YAxis stroke="#71717a" fontSize={10} />
              <Tooltip
                contentStyle={tooltipStyle}
                labelFormatter={(value) =>
                  `Time: ${new Date(String(value)).toLocaleString()}`
                }
                formatter={(value) => [
                  Number(value).toLocaleString(),
                  "Row Count",
                ]}
              />
              <Area
                type="monotone"
                dataKey="rows_count"
                stroke="#8b5cf6"
                fill="#8b5cf6"
                fillOpacity={0.1}
                strokeWidth={2}
              />
            </AreaChart>
          </ResponsiveContainer>
        </div>
      </div>
    );
  };

  return (
    <div className="bg-card border border-border rounded-xl p-5 shadow-sm">
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 mb-6 pb-4 border-b border-border">
        <div className="flex bg-zinc-900 p-1 rounded-lg border border-border">
          <button
            type="button"
            onClick={() => setViewMode("activity")}
            className={`px-3.5 py-1.5 text-xs font-semibold rounded-md transition-all duration-200 ${
              viewMode === "activity"
                ? "bg-zinc-800 text-foreground shadow-sm"
                : "text-muted-foreground hover:text-foreground"
            }`}
          >
            System Activity
          </button>
          <button
            type="button"
            onClick={() => setViewMode("database")}
            className={`px-3.5 py-1.5 text-xs font-semibold rounded-md transition-all duration-200 ${
              viewMode === "database"
                ? "bg-zinc-800 text-foreground shadow-sm"
                : "text-muted-foreground hover:text-foreground"
            }`}
          >
            Database Growth
          </button>
        </div>

        <div className="flex items-center gap-2">
          <Calendar size={14} className="text-muted-foreground" />
          <span className="text-xs text-muted-foreground mr-1">
            Time Range:
          </span>
          <div className="flex bg-zinc-900 p-1 rounded-lg border border-border">
            {(["1h", "3h", "6h", "12h", "24h"] as TimeRange[]).map((range) => (
              <button
                key={range}
                type="button"
                onClick={() => setTimeRange(range)}
                className={`px-2.5 py-1 text-xs font-mono rounded-md transition-all duration-200 ${
                  timeRange === range
                    ? "bg-zinc-800 text-foreground shadow-sm font-bold"
                    : "text-muted-foreground hover:text-foreground"
                }`}
              >
                {range}
              </button>
            ))}
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
        <div className="lg:col-span-9">{renderActiveCharts()}</div>

        <div className="lg:col-span-3 lg:border-l lg:border-border lg:pl-6 flex flex-col justify-start">
          <h4 className="text-xs font-semibold text-muted-foreground uppercase tracking-wider mb-3">
            {viewMode === "activity" ? "System activity" : "Database status"}
          </h4>

          {viewMode === "activity" ? (
            <div className="flex flex-col gap-2">
              {ACTIVITY_CATEGORIES.map((cat) => {
                const isActive = activityCategory === cat.id;
                const catSubs = subMetrics[cat.id];
                return (
                  <div
                    key={cat.id}
                    className={`rounded-lg border transition-colors ${
                      isActive
                        ? "bg-zinc-800/50 border-primary/40"
                        : "border-border/60"
                    }`}
                  >
                    <button
                      type="button"
                      onClick={() => setActivityCategory(cat.id)}
                      className={`w-full text-left px-3 py-2 rounded-lg transition-colors ${
                        isActive
                          ? "text-foreground"
                          : "text-foreground/90 hover:bg-zinc-900/60"
                      }`}
                    >
                      <span className="text-xs font-semibold">{cat.label}</span>
                    </button>
                    {isActive && (
                      <div className="px-3 pb-2.5 flex flex-col gap-1 border-t border-border/50 mt-0.5 pt-2">
                        {cat.subs.map((sub) => (
                          <label
                            key={sub.id}
                            className="flex items-center gap-2 cursor-pointer text-xs text-foreground/90 hover:text-foreground"
                          >
                            <input
                              type="checkbox"
                              checked={catSubs[sub.id as keyof typeof catSubs]}
                              onChange={() =>
                                toggleSubMetric(
                                  cat.id,
                                  sub.id as keyof SubMetricsState[typeof cat.id],
                                )
                              }
                              className="rounded bg-zinc-950 border-zinc-700 text-primary focus:ring-primary"
                            />
                            <span>{sub.label}</span>
                          </label>
                        ))}
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          ) : (
            <div className="text-xs text-muted-foreground space-y-2 bg-zinc-950/40 p-3 rounded-lg border border-border/60">
              <p>
                Database size and row counts are gathered automatically in the
                background by the LogLite diagnostics engine.
              </p>
              <p className="text-zinc-500 mt-1">
                Vacuuming triggers automatically or on a schedule to recycle
                unused SQLite database pages.
              </p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
