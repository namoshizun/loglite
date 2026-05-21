import { useQuery } from '@tanstack/react-query';
import { fetchHealth, fetchStats, fetchVersion } from '../api/client';
import { Database, Layers, Moon, Sun } from 'lucide-react';
import { useTheme, type Theme } from '../theme';
import { useI18n, type Locale } from '../i18n/locale';
import { formatBytes } from '../utils/formatBytes';

export default function Header() {
  const { theme, setTheme } = useTheme();
  const { locale, setLocale, t } = useI18n();

  const { data: healthData, isError: isHealthError } = useQuery({
    queryKey: ['health'],
    queryFn: fetchHealth,
    refetchInterval: 5000,
    retry: true,
  });

  const { data: versionData } = useQuery({
    queryKey: ['version'],
    queryFn: fetchVersion,
    staleTime: Infinity,
  });

  const now = new Date();
  const oneHourAgo = new Date(now.getTime() - 60 * 60 * 1000);

  const { data: statsData } = useQuery({
    queryKey: ['headerStats'],
    queryFn: () => fetchStats(oneHourAgo.toISOString(), now.toISOString()),
    refetchInterval: 30000,
  });

  const isHealthy = healthData?.status === 'ok' && !isHealthError;
  const dbStats = statsData?.database?.[statsData.database.length - 1];
  const rowCount = dbStats?.rows_count ?? 0;
  const dbSizeBytes = dbStats?.db_size ?? 0;

  return (
    <header className="border-b border-border bg-card px-6 py-4 flex flex-col sm:flex-row items-center justify-between gap-4">
      <div className="flex items-center gap-3">
        <div className="bg-primary/10 text-primary p-2.5 rounded-lg border border-primary/20">
          <Layers size={22} className="animate-pulse" />
        </div>
        <div>
          <h1 className="text-xl font-bold tracking-tight m-0 text-foreground flex items-center gap-2.5">
            LogLite
            <span
              className="relative flex h-2.5 w-2.5 shrink-0"
              title={isHealthy ? t('header.serverOnline') : t('header.serverOffline')}
              aria-label={isHealthy ? t('header.serverOnline') : t('header.serverOffline')}
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
            {versionData?.version && (
              <span className="font-mono text-muted-foreground border-l border-border">
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
              { id: 'en' as Locale, label: t('lang.en') },
              { id: 'zh' as Locale, label: t('lang.zh') },
            ] as const
          ).map(({ id, label }) => (
            <button
              key={id}
              type="button"
              onClick={() => setLocale(id)}
              className={`px-2.5 py-1 text-xs font-semibold rounded-md transition-all duration-200 ${
                locale === id
                  ? 'bg-card text-foreground shadow-sm'
                  : 'text-muted-foreground hover:text-foreground'
              }`}
              aria-pressed={locale === id}
            >
              {label}
            </button>
          ))}
        </div>

        <div className="flex bg-muted p-1 rounded-lg border border-border">
          {(
            [
              { id: 'dark' as Theme, label: t('theme.dark'), icon: Moon },
              { id: 'light' as Theme, label: t('theme.light'), icon: Sun },
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

        <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-muted border border-border">
          <Database size={16} className="text-blue-400" />
          <div className="text-left">
            <div className="text-xs text-muted-foreground">{t('header.dbSize')}</div>
            <div className="text-xs font-mono font-semibold">{formatBytes(dbSizeBytes, t)}</div>
          </div>
        </div>

        <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-muted border border-border">
          <Layers size={16} className="text-purple-400" />
          <div className="text-left">
            <div className="text-xs text-muted-foreground">{t('header.logCountLabel')}</div>
            <div className="text-xs font-mono font-semibold">
              {t('header.logCountValue', { n: rowCount.toLocaleString() })}
            </div>
          </div>
        </div>
      </div>
    </header>
  );
}
