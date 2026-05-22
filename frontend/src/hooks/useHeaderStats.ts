import { useQuery } from '@tanstack/react-query';
import { fetchStats } from '../api/client';

const HEADER_STATS_KEY = ['headerStats'] as const;
const REFETCH_MS = 30_000;
const WINDOW_MS = 60 * 60 * 1000;

export function useHeaderStats() {
  return useQuery({
    queryKey: HEADER_STATS_KEY,
    queryFn: () => {
      const until = new Date();
      const since = new Date(until.getTime() - WINDOW_MS);
      return fetchStats(since.toISOString(), until.toISOString());
    },
    refetchInterval: REFETCH_MS,
  });
}
