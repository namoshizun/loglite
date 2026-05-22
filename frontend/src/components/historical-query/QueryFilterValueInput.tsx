import type { LogColumnKind } from '../../api/client';
import { useI18n } from '../../i18n/locale';
import FilterDateTimeInput from '../FilterDateTimeInput';
import { filterInputClassName } from './constants';

type QueryFilterValueInputProps = {
  kind: LogColumnKind;
  value: string;
  onChange: (value: string) => void;
  onTouched: () => void;
};

export default function QueryFilterValueInput({
  kind,
  value,
  onChange,
  onTouched,
}: QueryFilterValueInputProps) {
  const { t } = useI18n();

  const touch = (next: string) => {
    onChange(next);
    onTouched();
  };

  if (kind === 'boolean') {
    return (
      <select
        value={value}
        onChange={(e) => touch(e.target.value)}
        className={`${filterInputClassName} font-mono`}
      >
        <option value="">{t('query.selectBoolean')}</option>
        <option value="true">{t('settings.true')}</option>
        <option value="false">{t('settings.false')}</option>
      </select>
    );
  }

  if (kind === 'integer') {
    return (
      <input
        type="number"
        step={1}
        placeholder={t('query.valuePlaceholderInteger')}
        value={value}
        onChange={(e) => touch(e.target.value)}
        className={filterInputClassName}
      />
    );
  }

  if (kind === 'number') {
    return (
      <input
        type="number"
        step="any"
        placeholder={t('query.valuePlaceholderNumber')}
        value={value}
        onChange={(e) => touch(e.target.value)}
        className={filterInputClassName}
      />
    );
  }

  if (kind === 'datetime') {
    return <FilterDateTimeInput value={value} onChange={onChange} onInteraction={onTouched} />;
  }

  const placeholder =
    kind === 'json' ? t('query.valuePlaceholderJson') : t('query.valuePlaceholder');

  return (
    <input
      type="text"
      placeholder={placeholder}
      value={value}
      onChange={(e) => touch(e.target.value)}
      className={filterInputClassName}
    />
  );
}
