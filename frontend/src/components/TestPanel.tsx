import { useEffect, useMemo, useState } from 'react';
import { useMutation } from '@tanstack/react-query';
import { postLog } from '../api/client';
import { useLogSchema } from '../hooks/useLogSchema';
import { useI18n } from '../i18n/locale';
import {
  buildDefaultFieldValues,
  buildLogPayload,
  canSubmitLog,
  editableSchemaColumns,
  type FieldValues,
} from '../utils/logPayloadField';
import LogInsertFieldInput from './test-send/LogInsertFieldInput';
import { Send } from 'lucide-react';

export default function TestPanel() {
  const { t } = useI18n();
  const [values, setValues] = useState<FieldValues>({});
  const [submitError, setSubmitError] = useState<string | null>(null);

  const { schemaColumns, isLoading: schemaLoading, isError: schemaError } = useLogSchema();

  const columns = useMemo(() => editableSchemaColumns(schemaColumns), [schemaColumns]);

  useEffect(() => {
    if (schemaColumns.length > 0) {
      setValues(buildDefaultFieldValues(schemaColumns));
      setSubmitError(null);
    }
  }, [schemaColumns]);

  const mutation = useMutation({
    mutationFn: postLog,
    onSuccess: () => {
      setSubmitError(null);
      if (schemaColumns.length > 0) {
        setValues(buildDefaultFieldValues(schemaColumns));
      }
    },
    onError: (err: Error) => {
      setSubmitError(err.message);
    },
  });

  const canSubmit = schemaColumns.length > 0 ? canSubmitLog(schemaColumns, values) : false;

  const handleSubmit = () => {
    if (!schemaColumns.length) return;
    const { payload, errorField } = buildLogPayload(schemaColumns, values);
    if (!payload) {
      setSubmitError(
        errorField ? t('test.invalidField', { field: errorField }) : t('test.invalidPayload'),
      );
      return;
    }
    setSubmitError(null);
    mutation.mutate(payload);
  };

  const setField = (name: string, next: string) => {
    setValues((prev) => ({ ...prev, [name]: next }));
  };

  if (schemaLoading) {
    return (
      <div className="bg-card border border-border rounded-xl p-8 text-center text-muted-foreground text-sm">
        {t('test.loadingSchema')}
      </div>
    );
  }

  if (schemaError) {
    return (
      <div className="bg-card border border-border rounded-xl p-8 text-center text-destructive text-sm">
        {t('test.schemaLoadFailed')}
      </div>
    );
  }

  return (
    <div className="bg-card border border-border rounded-xl p-5 shadow-sm space-y-5">
      <div className="flex flex-wrap items-center gap-3">
        <button
          type="button"
          disabled={!canSubmit || mutation.isPending}
          onClick={handleSubmit}
          className="bg-primary hover:bg-primary/95 text-primary-foreground font-bold px-4 py-2 rounded text-xs flex items-center gap-2 transition-colors cursor-pointer disabled:opacity-40 disabled:cursor-not-allowed"
        >
          <Send size={14} />
          {mutation.isPending ? t('test.sending') : t('test.send')}
        </button>

        {mutation.isSuccess && (
          <span className="text-xs text-green-600 dark:text-green-400 font-medium">
            {t('test.success')}
          </span>
        )}

        {(submitError || mutation.isError) && (
          <span className="text-xs text-destructive">{submitError ?? mutation.error?.message}</span>
        )}
      </div>

      <div className="overflow-x-auto border border-border rounded-lg">
        <table className="w-full text-left text-xs border-collapse">
          <thead>
            <tr className="bg-muted border-b border-border">
              <th className="py-2.5 px-4 font-mono font-bold text-muted-foreground w-[140px]">
                {t('test.colField')}
              </th>
              <th className="py-2.5 px-4 font-mono font-bold text-muted-foreground w-[100px]">
                {t('test.colType')}
              </th>
              <th className="py-2.5 px-4 font-mono font-bold text-muted-foreground w-[90px]">
                {t('test.colRequired')}
              </th>
              <th className="py-2.5 px-4 font-mono font-bold text-muted-foreground">
                {t('test.colValue')}
              </th>
            </tr>
          </thead>
          <tbody className="divide-y divide-border/60">
            {columns.map((col) => (
              <tr key={col.name} className="align-top">
                <td className="py-3 px-4 font-mono text-foreground">{col.name}</td>
                <td className="py-3 px-4 font-mono text-muted-foreground">{col.kind}</td>
                <td className="py-3 px-4">
                  {col.not_null ? (
                    <span className="text-[10px] font-semibold uppercase tracking-wide text-primary">
                      {t('test.required')}
                    </span>
                  ) : (
                    <span className="text-[10px] text-muted-foreground">{t('test.optional')}</span>
                  )}
                </td>
                <td className="py-3 px-4 min-w-[240px]">
                  <LogInsertFieldInput
                    column={col}
                    value={values[col.name] ?? ''}
                    onChange={(next) => setField(col.name, next)}
                  />
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
