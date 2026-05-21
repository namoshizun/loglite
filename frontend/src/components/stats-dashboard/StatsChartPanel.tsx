import type { ReactNode } from 'react';
import { ResponsiveContainer } from 'recharts';
import type { ActivityStatRecord, DatabaseStatRecord } from '../../api/client';
import { useI18n } from '../../i18n/locale';
import ActivityCategoryChart from './ActivityCategoryChart';
import { enrichActivityRows } from './constants';
import DatabaseGrowthCharts from './DatabaseGrowthCharts';
import type { ActivityCategory, SubMetricsState, ViewMode } from './types';

type StatsChartPanelProps = {
  viewMode: ViewMode;
  isLoading: boolean;
  isError: boolean;
  errorMessage: string | undefined;
  activities: ActivityStatRecord[] | undefined;
  database: DatabaseStatRecord[] | undefined;
  activityCategory: ActivityCategory;
  subMetrics: SubMetricsState;
};

function ChartPlaceholder({ children }: { children: ReactNode }) {
  return (
    <div className="h-[300px] flex items-center justify-center text-muted-foreground bg-muted/40 border border-border rounded-lg">
      {children}
    </div>
  );
}

export default function StatsChartPanel({
  viewMode,
  isLoading,
  isError,
  errorMessage,
  activities,
  database,
  activityCategory,
  subMetrics,
}: StatsChartPanelProps) {
  const { t } = useI18n();

  if (isLoading) {
    return (
      <ChartPlaceholder>
        <div className="flex flex-col items-center gap-2">
          <div className="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin" />
          <span>{t('stats.loading')}</span>
        </div>
      </ChartPlaceholder>
    );
  }

  if (isError) {
    return (
      <div className="h-[300px] flex items-center justify-center text-destructive bg-destructive/5 border border-destructive/20 rounded-lg p-6 text-center">
        <div>
          <p className="font-semibold">{t('stats.loadFailed')}</p>
          <p className="text-xs text-muted-foreground mt-1">{errorMessage ?? 'Unknown error'}</p>
        </div>
      </div>
    );
  }

  if (viewMode === 'activity') {
    if (!activities?.length) {
      return <ChartPlaceholder>{t('stats.noActivity')}</ChartPlaceholder>;
    }

    const chartData = enrichActivityRows(activities);
    const enabledSubs = subMetrics[activityCategory];
    const hasEnabledSub = Object.values(enabledSubs).some(Boolean);

    if (!hasEnabledSub) {
      return <ChartPlaceholder>{t('stats.selectMetric')}</ChartPlaceholder>;
    }

    return (
      <div className="h-[300px] w-full">
        <ResponsiveContainer width="100%" height="100%">
          <ActivityCategoryChart
            category={activityCategory}
            subMetrics={subMetrics}
            data={chartData}
          />
        </ResponsiveContainer>
      </div>
    );
  }

  if (!database?.length) {
    return <ChartPlaceholder>{t('stats.noDatabase')}</ChartPlaceholder>;
  }

  return <DatabaseGrowthCharts rows={database} />;
}
