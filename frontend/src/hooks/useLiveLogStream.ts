import { useEffect, useRef, useState } from 'react';
import { getSSEUrl } from '../api/client';

export type LiveLogRecord = {
  id: number;
  timestamp: string;
  level: string;
  service: string;
  message: string;
  [key: string]: unknown;
};

export function useLiveLogStream(isPaused: boolean, maxLogs = 500) {
  const [logs, setLogs] = useState<LiveLogRecord[]>([]);
  const bufferRef = useRef<LiveLogRecord[]>([]);
  const eventSourceRef = useRef<EventSource | null>(null);

  useEffect(() => {
    if (isPaused) {
      eventSourceRef.current?.close();
      eventSourceRef.current = null;
      return;
    }

    const es = new EventSource(getSSEUrl('*'));
    eventSourceRef.current = es;

    es.onmessage = (event) => {
      try {
        const incoming = JSON.parse(event.data) as LiveLogRecord[];
        if (incoming?.length > 0) {
          const ascending = [...incoming].reverse();
          bufferRef.current = [...bufferRef.current, ...ascending].slice(-maxLogs);
          setLogs([...bufferRef.current]);
        }
      } catch (err) {
        console.error('Failed to parse SSE payload:', err);
      }
    };

    es.onerror = (err) => {
      console.warn('SSE disconnected, browser will attempt reconnection:', err);
    };

    return () => es.close();
  }, [isPaused, maxLogs]);

  const clearLogs = () => {
    bufferRef.current = [];
    setLogs([]);
  };

  return { logs, clearLogs };
}
