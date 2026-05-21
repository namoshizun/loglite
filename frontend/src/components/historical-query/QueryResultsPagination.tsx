import { ChevronLeft, ChevronRight } from 'lucide-react';
import { useI18n } from '../../i18n/locale';

type QueryResultsPaginationProps = {
  offset: number;
  limit: number;
  total: number;
  onPageChange: (direction: 'prev' | 'next') => void;
};

export default function QueryResultsPagination({
  offset,
  limit,
  total,
  onPageChange,
}: QueryResultsPaginationProps) {
  const { t } = useI18n();
  const currentPage = Math.floor(offset / limit) + 1;
  const totalPages = Math.ceil(total / limit);

  return (
    <div className="flex items-center justify-between border-t border-border pt-4">
      <div className="text-xs text-muted-foreground font-mono">
        {t('query.pagination.showing')}{' '}
        <span className="text-foreground font-medium">{offset + 1}</span> {t('query.pagination.to')}{' '}
        <span className="text-foreground font-medium">{Math.min(offset + limit, total)}</span>{' '}
        {t('query.pagination.of')}{' '}
        <span className="text-foreground font-semibold">{total.toLocaleString()}</span>{' '}
        {t('query.pagination.entries')}
      </div>

      <div className="flex items-center gap-1.5">
        <button
          type="button"
          onClick={() => onPageChange('prev')}
          disabled={offset === 0}
          className="p-1 rounded bg-secondary border border-border text-muted-foreground hover:text-foreground disabled:opacity-30 hover:bg-muted transition-all cursor-pointer"
        >
          <ChevronLeft size={16} />
        </button>
        <span className="text-xs font-mono text-muted-foreground px-2">
          {t('query.pagination.page')}{' '}
          <strong className="text-foreground font-semibold">{currentPage}</strong>{' '}
          {t('query.pagination.ofPages')}{' '}
          <strong className="text-foreground font-semibold">{totalPages}</strong>
        </span>
        <button
          type="button"
          onClick={() => onPageChange('next')}
          disabled={offset + limit >= total}
          className="p-1 rounded bg-secondary border border-border text-muted-foreground hover:text-foreground disabled:opacity-30 hover:bg-muted transition-all cursor-pointer"
        >
          <ChevronRight size={16} />
        </button>
      </div>
    </div>
  );
}
