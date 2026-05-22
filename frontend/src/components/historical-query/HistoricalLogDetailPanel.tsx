import JsonViewer from '../JsonViewer';
import { useI18n } from '../../i18n/locale';

type HistoricalLogDetailPanelProps = {
  log: Record<string, unknown>;
  scalarColumns: string[];
  onClose: () => void;
};

export default function HistoricalLogDetailPanel({
  log,
  scalarColumns,
  onClose,
}: HistoricalLogDetailPanelProps) {
  const { t } = useI18n();

  return (
    <div className="w-full lg:w-[420px] bg-muted/30 border border-border rounded-lg p-4 flex flex-col gap-4 overflow-y-auto max-h-[600px]">
      <div className="flex items-center justify-between border-b border-border pb-2">
        <span className="text-xs font-bold text-foreground">{t('query.details')}</span>
        <button
          type="button"
          onClick={onClose}
          className="text-xs text-muted-foreground hover:text-foreground bg-secondary border border-border px-2 py-0.5 rounded transition-colors"
        >
          {t('query.close')}
        </button>
      </div>

      <div className="space-y-2 text-xs font-mono">
        {scalarColumns.map(
          (col) =>
            log[col] !== undefined &&
            typeof log[col] !== 'object' && (
              <div key={col} className="flex justify-between border-b border-border/30 py-1">
                <span className="text-muted-foreground select-none">{col}:</span>
                <span className="text-foreground break-all select-all ml-4 text-right">
                  {String(log[col])}
                </span>
              </div>
            ),
        )}
      </div>

      <div className="mt-2 flex-1">
        <JsonViewer data={log} title={t('query.fullMatrix')} />
      </div>
    </div>
  );
}
