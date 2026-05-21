import { Calendar } from 'lucide-react';
import { useI18n } from '../../i18n/locale';
import { TIME_RANGES, type TimeRange, type ViewMode } from './types';

type StatsDashboardHeaderProps = {
  viewMode: ViewMode;
  timeRange: TimeRange;
  onViewModeChange: (mode: ViewMode) => void;
  onTimeRangeChange: (range: TimeRange) => void;
};

const tabClass = (active: boolean) =>
  `px-3.5 py-1.5 text-xs font-semibold rounded-md transition-all duration-200 ${
    active ? 'bg-card text-foreground shadow-sm' : 'text-muted-foreground hover:text-foreground'
  }`;

const rangeClass = (active: boolean) =>
  `px-2.5 py-1 text-xs font-mono rounded-md transition-all duration-200 ${
    active
      ? 'bg-card text-foreground shadow-sm font-bold'
      : 'text-muted-foreground hover:text-foreground'
  }`;

export default function StatsDashboardHeader({
  viewMode,
  timeRange,
  onViewModeChange,
  onTimeRangeChange,
}: StatsDashboardHeaderProps) {
  const { t } = useI18n();

  return (
    <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 mb-6 pb-4 border-b border-border">
      <div className="flex bg-muted p-1 rounded-lg border border-border">
        <button
          type="button"
          onClick={() => onViewModeChange('activity')}
          className={tabClass(viewMode === 'activity')}
        >
          {t('stats.systemActivity')}
        </button>
        <button
          type="button"
          onClick={() => onViewModeChange('database')}
          className={tabClass(viewMode === 'database')}
        >
          {t('stats.databaseGrowth')}
        </button>
      </div>

      <div className="flex items-center gap-2">
        <Calendar size={14} className="text-muted-foreground" />
        <span className="text-xs text-muted-foreground mr-1">{t('stats.timeRange')}</span>
        <div className="flex bg-muted p-1 rounded-lg border border-border">
          {TIME_RANGES.map((range) => (
            <button
              key={range}
              type="button"
              onClick={() => onTimeRangeChange(range)}
              className={rangeClass(timeRange === range)}
            >
              {range}
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}
