import { useMemo, useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { fetchStats } from '../api/client';
import { useTheme } from '../theme';
import { useI18n } from '../i18n/locale';
import { DEFAULT_SUB_METRICS, getStatsTimeWindow } from './stats-dashboard/constants';
import StatsDashboardHeader from './stats-dashboard/StatsDashboardHeader';
import StatsChartPanel from './stats-dashboard/StatsChartPanel';
import StatsMetricSidebar from './stats-dashboard/StatsMetricSidebar';
import type {
  ActivityCategory,
  SubMetricsState,
  TimeRange,
  ViewMode,
} from './stats-dashboard/types';

export default function StatsDashboard() {
  useTheme();
  const { t } = useI18n();

  const activityCategories = useMemo(
    () => [
      {
        id: 'query' as ActivityCategory,
        label: t('stats.cat.query'),
        subs: [
          { id: 'count', label: t('stats.sub.requestCount') },
          { id: 'latency', label: t('stats.sub.latency') },
        ],
      },
      {
        id: 'ingestion' as ActivityCategory,
        label: t('stats.cat.ingestion'),
        subs: [
          { id: 'count', label: t('stats.sub.requestCount') },
          { id: 'size', label: t('stats.sub.payloadSize') },
        ],
      },
      {
        id: 'insertion' as ActivityCategory,
        label: t('stats.cat.insertion'),
        subs: [
          { id: 'count', label: t('stats.sub.logCount') },
          { id: 'cost', label: t('stats.sub.timeCost') },
        ],
      },
      {
        id: 'connections' as ActivityCategory,
        label: t('stats.cat.connections'),
        subs: [
          { id: 'http', label: t('stats.sub.http') },
          { id: 'sse', label: t('stats.sub.sse') },
        ],
      },
    ],
    [t],
  );

  const [timeRange, setTimeRange] = useState<TimeRange>('6h');
  const [viewMode, setViewMode] = useState<ViewMode>('activity');
  const [activityCategory, setActivityCategory] = useState<ActivityCategory>('query');
  const [subMetrics, setSubMetrics] = useState<SubMetricsState>(DEFAULT_SUB_METRICS);

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

  const { since, until } = getStatsTimeWindow(timeRange);

  const {
    data: stats,
    isLoading,
    isError,
    error,
  } = useQuery({
    queryKey: ['stats', timeRange],
    queryFn: () => fetchStats(since, until),
    refetchInterval: 15000,
  });

  return (
    <div className="bg-card border border-border rounded-xl p-5 shadow-sm">
      <StatsDashboardHeader
        viewMode={viewMode}
        timeRange={timeRange}
        onViewModeChange={setViewMode}
        onTimeRangeChange={setTimeRange}
      />

      <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
        <div className="lg:col-span-9">
          <StatsChartPanel
            viewMode={viewMode}
            isLoading={isLoading}
            isError={isError}
            errorMessage={(error as Error | undefined)?.message}
            activities={stats?.activities}
            database={stats?.database}
            activityCategory={activityCategory}
            subMetrics={subMetrics}
          />
        </div>

        <StatsMetricSidebar
          viewMode={viewMode}
          activityCategories={activityCategories}
          activityCategory={activityCategory}
          subMetrics={subMetrics}
          onCategoryChange={setActivityCategory}
          onToggleSubMetric={toggleSubMetric}
        />
      </div>
    </div>
  );
}
