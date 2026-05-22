import type { LogSchemaColumn } from '../../api/client';
import { useI18n } from '../../i18n/locale';
import FilterDateTimeInput from '../FilterDateTimeInput';
import { filterInputClassName } from '../historical-query/constants';

const LOG_LEVELS = ['TRACE', 'DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL'] as const;

type LogInsertFieldInputProps = {
  column: LogSchemaColumn;
  value: string;
  onChange: (value: string) => void;
};

export default function LogInsertFieldInput({ column, value, onChange }: LogInsertFieldInputProps) {
  const { t } = useI18n();
  const optional = !column.not_null;
  const emptyLabel = optional ? t('test.optionalEmpty') : '';

  if (column.name === 'level') {
    // just a convenient courtesy
    return (
      <select
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={`${filterInputClassName} font-mono w-full`}
      >
        {!column.not_null && <option value="">{emptyLabel}</option>}
        {LOG_LEVELS.map((level) => (
          <option key={level} value={level}>
            {level}
          </option>
        ))}
      </select>
    );
  }

  if (column.kind === 'boolean') {
    return (
      <select
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={`${filterInputClassName} font-mono w-full`}
      >
        {optional && <option value="">{emptyLabel}</option>}
        <option value="true">{t('settings.true')}</option>
        <option value="false">{t('settings.false')}</option>
      </select>
    );
  }

  if (column.kind === 'integer') {
    return (
      <input
        type="number"
        step={1}
        placeholder={optional ? t('test.optionalEmpty') : t('query.valuePlaceholderInteger')}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={`${filterInputClassName} w-full`}
      />
    );
  }

  if (column.kind === 'number') {
    return (
      <input
        type="number"
        step="any"
        placeholder={optional ? t('test.optionalEmpty') : t('query.valuePlaceholderNumber')}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={`${filterInputClassName} w-full`}
      />
    );
  }

  if (column.kind === 'datetime') {
    return <FilterDateTimeInput value={value} onChange={onChange} />;
  }

  if (column.kind === 'json') {
    return (
      <textarea
        rows={3}
        placeholder={t('test.jsonPlaceholder')}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className={`${filterInputClassName} w-full font-mono resize-y min-h-[4rem]`}
      />
    );
  }

  const placeholder =
    column.kind === 'text'
      ? optional
        ? t('test.optionalEmpty')
        : t('query.valuePlaceholder')
      : t('query.valuePlaceholder');

  return (
    <input
      type="text"
      placeholder={placeholder}
      value={value}
      onChange={(e) => onChange(e.target.value)}
      className={`${filterInputClassName} w-full`}
    />
  );
}
