import { useQuery } from '@tanstack/react-query';
import { fetchHealth, fetchStats, fetchVersion } from '../api/client';
import { Database, Layers, Radio } from 'lucide-react';

export function formatBytes(bytes: number, decimals = 2) {
  if (!bytes || bytes === 0) return '0 Bytes';
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

export default function Header() {
  // Poll health every 10 seconds
  const { data: healthData, isError: isHealthError } = useQuery({
    queryKey: ['health'],
    queryFn: fetchHealth,
    refetchInterval: 10000,
    retry: true,
  });

  const { data: versionData } = useQuery({
    queryKey: ['version'],
    queryFn: fetchVersion,
    staleTime: Infinity,
  });

  // Fetch stats for the last hour to display database summary metrics
  const now = new Date();
  const oneHourAgo = new Date(now.getTime() - 60 * 60 * 1000);
  
  const { data: statsData } = useQuery({
    queryKey: ['headerStats'],
    queryFn: () => fetchStats(oneHourAgo.toISOString(), now.toISOString()),
    refetchInterval: 30000, // Poll every 30 seconds
  });

  const isHealthy = healthData?.status === 'ok' && !isHealthError;
  
  // Get latest db stats row
  const dbStats = statsData?.database?.[statsData.database.length - 1];
  const rowCount = dbStats?.rows_count ?? 0;
  const dbSizeBytes = dbStats?.db_size ?? 0;

  return (
    <header className="border-b border-border bg-card px-6 py-4 flex flex-col sm:flex-row items-center justify-between gap-4">
      {/* Brand Logo & Name */}
      <div className="flex items-center gap-3">
        <div className="bg-primary/10 text-primary p-2.5 rounded-lg border border-primary/20">
          <Layers size={22} className="animate-pulse" />
        </div>
        <div>
          <h1 className="text-xl font-bold tracking-tight m-0 text-foreground flex items-center gap-2">
            LogLite <span className="text-xs font-mono px-2 py-0.5 rounded bg-zinc-800 text-zinc-400 border border-zinc-700">Admin</span>
          </h1>
          <p className="text-xs text-muted-foreground flex items-center gap-2 flex-wrap">
            <span>Lightweight SQLite Log Dashboard</span>
            {versionData?.version && (
              <span className="font-mono text-zinc-500 border-l border-border pl-2">
                v{versionData.version}
              </span>
            )}
          </p>
        </div>
      </div>

      {/* Stats Summary Panel */}
      <div className="flex flex-wrap items-center gap-4 sm:gap-6">
        {/* Database Size Metric */}
        <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-zinc-900 border border-border">
          <Database size={16} className="text-blue-400" />
          <div className="text-left">
            <div className="text-xs text-muted-foreground">DB Size</div>
            <div className="text-xs font-mono font-semibold">{formatBytes(dbSizeBytes)}</div>
          </div>
        </div>

        {/* Database Rows Metric */}
        <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-zinc-900 border border-border">
          <Layers size={16} className="text-purple-400" />
          <div className="text-left">
            <div className="text-xs text-muted-foreground">Log Count</div>
            <div className="text-xs font-mono font-semibold">{rowCount.toLocaleString()} rows</div>
          </div>
        </div>

        {/* Status Pill */}
        <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-zinc-900 border border-border">
          <Radio size={16} className={isHealthy ? "text-green-500 animate-ping" : "text-red-500"} />
          <div className="text-left flex items-center gap-2">
            <span className="text-xs text-muted-foreground">Server:</span>
            <span className={`text-xs font-bold ${isHealthy ? 'text-green-400' : 'text-red-400'}`}>
              {isHealthy ? 'ONLINE' : 'OFFLINE'}
            </span>
          </div>
        </div>
      </div>
    </header>
  );
}
