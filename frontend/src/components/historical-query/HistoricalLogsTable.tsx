import { Eye } from 'lucide-react';
import { useI18n } from '../../i18n/locale';
import LogTableCell from './LogTableCell';

type HistoricalLogsTableProps = {
  visibleColumns: string[];
  rows: Record<string, unknown>[] | undefined;
  isLoading: boolean;
  isError: boolean;
  errorMessage: string | undefined;
  selectedLogId: unknown;
  levelColors: Record<string, string>;
  serviceTagClass: string;
  onSelectRow: (row: Record<string, unknown>) => void;
};

export default function HistoricalLogsTable({
  visibleColumns,
  rows,
  isLoading,
  isError,
  errorMessage,
  selectedLogId,
  levelColors,
  serviceTagClass,
  onSelectRow,
}: HistoricalLogsTableProps) {
  const { t } = useI18n();
  const colSpan = visibleColumns.length + 1;

  return (
    <div className="flex-1 overflow-x-auto border border-border rounded-lg bg-background">
      <table className="w-full text-left text-xs border-collapse">
        <thead>
          <tr className="bg-muted border-b border-border">
            {visibleColumns.map((col) => (
              <th
                key={col}
                className="py-2.5 px-4 font-mono font-bold text-muted-foreground capitalize select-none"
              >
                {col}
              </th>
            ))}
            <th className="py-2.5 px-4 text-right"></th>
          </tr>
        </thead>
        <tbody className="divide-y divide-border/60">
          {isLoading ? (
            <tr>
              <td colSpan={colSpan} className="py-20 text-center text-muted-foreground">
                <div className="flex flex-col items-center gap-2">
                  <div className="w-6 h-6 border-2 border-primary border-t-transparent rounded-full animate-spin"></div>
                  <span>{t('query.loading')}</span>
                </div>
              </td>
            </tr>
          ) : isError ? (
            <tr>
              <td colSpan={colSpan} className="py-20 text-center text-destructive">
                <p className="font-semibold">{t('query.failed')}</p>
                <p className="text-xs text-muted-foreground mt-1">
                  {errorMessage ?? 'Unknown error'}
                </p>
              </td>
            </tr>
          ) : !rows || rows.length === 0 ? (
            <tr>
              <td colSpan={colSpan} className="py-20 text-center text-muted-foreground italic">
                {t('query.noResults')}
              </td>
            </tr>
          ) : (
            rows.map((row, idx) => (
              <tr
                key={(row.id as string | number | undefined) ?? idx}
                onClick={() => onSelectRow(row)}
                className={`hover:bg-muted cursor-pointer border-l-2 border-transparent transition-colors ${
                  selectedLogId !== undefined && row.id === selectedLogId
                    ? 'bg-muted border-l-primary'
                    : ''
                }`}
              >
                {visibleColumns.map((col) => (
                  <LogTableCell
                    key={col}
                    column={col}
                    value={row[col]}
                    levelColors={levelColors}
                    serviceTagClass={serviceTagClass}
                  />
                ))}
                <td className="py-2 px-4 text-right text-muted-foreground">
                  <Eye
                    size={13}
                    className="inline opacity-0 group-hover:opacity-100 hover:text-foreground transition-opacity"
                  />
                </td>
              </tr>
            ))
          )}
        </tbody>
      </table>
    </div>
  );
}
