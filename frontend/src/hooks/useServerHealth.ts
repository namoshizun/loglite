import { useQuery } from '@tanstack/react-query';
import { fetchHealth } from '../api/client';

export function useServerHealth() {
  const query = useQuery({
    queryKey: ['health'],
    queryFn: fetchHealth,
    refetchInterval: 5000,
    retry: false,
  });

  const isHealthy = query.data?.status === 'ok' && !query.isError && !query.isRefetchError;

  return { ...query, isHealthy };
}
