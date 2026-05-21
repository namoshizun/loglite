import { Settings2 } from 'lucide-react';
import { useI18n } from '../../i18n/locale';

type QueryResultsToolbarProps = {
  columns: string[];
  visibleColumns: string[];
  showColumnSettings: boolean;
  onToggleColumnSettings: () => void;
  onToggleColumn: (col: string) => void;
  total: number | undefined;
  limit: number;
  onLimitChange: (limit: number) => void;
};

export default function QueryResultsToolbar({
  columns,
  visibleColumns,
  showColumnSettings,
  onToggleColumnSettings,
  onToggleColumn,
  total,
  limit,
  onLimitChange,
}: QueryResultsToolbarProps) {
  const { t } = useI18n();

  return (
    <div className="flex justify-between items-center bg-muted/30 p-2.5 border border-border rounded-lg">
      <div className="relative">
        <button
          type="button"
          onClick={onToggleColumnSettings}
          className="flex items-center gap-1.5 px-3 py-1 text-xs font-medium bg-card border border-border hover:bg-muted text-muted-foreground hover:text-foreground rounded transition-colors"
        >
          <Settings2 size={13} /> {t('query.visibleColumns')}
        </button>

        {showColumnSettings && (
          <div className="absolute z-10 top-8 left-0 bg-card border border-border p-3 rounded-lg shadow-lg w-48 text-xs flex flex-col gap-1.5">
            <span className="text-[10px] text-muted-foreground font-bold uppercase tracking-wider mb-1">
              {t('query.selectColumns')}
            </span>
            {columns.map((col) => (
              <label
                key={col}
                className="flex items-center gap-2 cursor-pointer text-muted-foreground hover:text-foreground"
              >
                <input
                  type="checkbox"
                  checked={visibleColumns.includes(col)}
                  onChange={() => onToggleColumn(col)}
                  className="rounded bg-background border-border text-primary focus:ring-primary"
                />
                <span className="font-mono">{col}</span>
              </label>
            ))}
          </div>
        )}
      </div>

      <div className="flex items-center gap-3 text-xs">
        {total !== undefined && (
          <span className="text-muted-foreground font-mono">
            {t('query.totalMatch')}{' '}
            <strong className="text-foreground font-semibold">{total.toLocaleString()}</strong> rows
          </span>
        )}
        <span className="text-muted-foreground border-l border-border pl-3">{t('query.rows')}</span>
        <select
          value={limit}
          onChange={(e) => onLimitChange(Number(e.target.value))}
          className="bg-background border border-border text-xs px-2 py-0.5 rounded text-foreground font-mono"
        >
          {[20, 50, 100].map((num) => (
            <option key={num} value={num}>
              {num}
            </option>
          ))}
        </select>
      </div>
    </div>
  );
}
