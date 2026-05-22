import { useI18n } from '../../i18n/locale';
import type { ActivityCategory, ActivityCategoryConfig, SubMetricsState, ViewMode } from './types';

type StatsMetricSidebarProps = {
  viewMode: ViewMode;
  activityCategories: ActivityCategoryConfig[];
  activityCategory: ActivityCategory;
  subMetrics: SubMetricsState;
  onCategoryChange: (category: ActivityCategory) => void;
  onToggleSubMetric: <C extends ActivityCategory>(
    category: C,
    subId: keyof SubMetricsState[C],
  ) => void;
};

export default function StatsMetricSidebar({
  viewMode,
  activityCategories,
  activityCategory,
  subMetrics,
  onCategoryChange,
  onToggleSubMetric,
}: StatsMetricSidebarProps) {
  const { t } = useI18n();

  return (
    <div className="lg:col-span-3 lg:border-l lg:border-border lg:pl-6 flex flex-col justify-start">
      <h4 className="text-xs font-semibold text-muted-foreground uppercase tracking-wider mb-3">
        {viewMode === 'activity' ? t('stats.metricCategory') : t('stats.databaseStatus')}
      </h4>

      {viewMode === 'activity' ? (
        <div className="flex flex-col gap-2">
          {activityCategories.map((cat) => {
            const isActive = activityCategory === cat.id;
            const catSubs = subMetrics[cat.id];
            return (
              <div
                key={cat.id}
                className={`rounded-lg border transition-colors ${
                  isActive ? 'bg-card/80 border-primary/40' : 'border-border/60'
                }`}
              >
                <button
                  type="button"
                  onClick={() => onCategoryChange(cat.id)}
                  className={`w-full text-left px-3 py-2 rounded-lg transition-colors ${
                    isActive ? 'text-foreground' : 'text-foreground/90 hover:bg-muted'
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
                            onToggleSubMetric(
                              cat.id,
                              sub.id as keyof SubMetricsState[typeof cat.id],
                            )
                          }
                          className="rounded bg-background border-border text-primary focus:ring-primary"
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
        <div className="text-xs text-muted-foreground space-y-2 bg-muted/50 p-3 rounded-lg border border-border/60">
          <p>{t('stats.dbStatusHint')}</p>
          <p className="text-muted-foreground mt-1">{t('stats.vacuumHint')}</p>
        </div>
      )}
    </div>
  );
}
