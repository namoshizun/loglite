import { useQuery } from '@tanstack/react-query';
import { fetchHealth, fetchStats, fetchVersion } from '../api/client';
import { Database, Layers, Moon, Sun } from 'lucide-react';
import { useTheme, type Theme } from '../theme';

export function formatBytes(bytes: number, decimals = 2) {
  if (!bytes || bytes === 0) return '0 Bytes';
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

export default function Header() {
  const { theme, setTheme } = useTheme();
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
          <h1 className="text-xl font-bold tracking-tight m-0 text-foreground flex items-center gap-2.5">
            LogLite
            <span
              className="relative flex h-2.5 w-2.5 shrink-0"
              title={isHealthy ? 'Server online' : 'Server offline'}
              aria-label={isHealthy ? 'Server online' : 'Server offline'}
            >
              {isHealthy && (
                <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-green-500 opacity-75" />
              )}
              <span
                className={`relative inline-flex h-2.5 w-2.5 rounded-full ${
                  isHealthy ? 'bg-green-500' : 'bg-red-500'
                }`}
              />
            </span>
          </h1>
          <p className="text-xs text-muted-foreground flex items-center gap-2 flex-wrap">
            <span>Lightweight SQLite Log Dashboard</span>
            {versionData?.version && (
              <span className="font-mono text-muted-foreground border-l border-border pl-2">
                v{versionData.version}
              </span>
            )}
          </p>
        </div>
      </div>

      <div className="flex flex-wrap items-center gap-4 sm:gap-6">
        <div className="flex bg-muted p-1 rounded-lg border border-border">
          {(
            [
              { id: 'dark' as Theme, label: 'Dark', icon: Moon },
              { id: 'light' as Theme, label: 'Light', icon: Sun },
            ] as const
          ).map(({ id, label, icon: Icon }) => (
            <button
              key={id}
              type="button"
              onClick={() => setTheme(id)}
              className={`flex items-center gap-1.5 px-2.5 py-1 text-xs font-semibold rounded-md transition-all duration-200 ${
                theme === id
                  ? 'bg-card text-foreground shadow-sm'
                  : 'text-muted-foreground hover:text-foreground'
              }`}
              aria-pressed={theme === id}
            >
              <Icon size={13} />
              {label}
            </button>
          ))}
        </div>

        {/* Database Size Metric */}
        <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-muted border border-border">
          <Database size={16} className="text-blue-400" />
          <div className="text-left">
            <div className="text-xs text-muted-foreground">DB Size</div>
            <div className="text-xs font-mono font-semibold">{formatBytes(dbSizeBytes)}</div>
          </div>
        </div>

        {/* Database Rows Metric */}
        <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-muted border border-border">
          <Layers size={16} className="text-purple-400" />
          <div className="text-left">
            <div className="text-xs text-muted-foreground">Log Count</div>
            <div className="text-xs font-mono font-semibold">{rowCount.toLocaleString()} rows</div>
          </div>
        </div>
      </div>
    </header>
  );
}
