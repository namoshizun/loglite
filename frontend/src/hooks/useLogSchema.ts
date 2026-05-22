import { useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import { fetchLogSchema } from '../api/client';
import type { LogSchemaColumn } from '../api/client';

export function useLogSchema() {
  const query = useQuery({
    queryKey: ['logSchema'],
    queryFn: fetchLogSchema,
  });

  const schemaColumns = useMemo(() => query.data?.columns ?? [], [query.data?.columns]);

  const columnByName = useMemo(() => {
    const map = new Map<string, LogSchemaColumn>();
    for (const col of schemaColumns) map.set(col.name, col);
    return map;
  }, [schemaColumns]);

  return {
    ...query,
    schema: query.data,
    schemaColumns,
    columnByName,
  };
}
