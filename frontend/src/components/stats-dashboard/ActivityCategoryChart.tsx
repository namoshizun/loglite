import { Area, Bar, ComposedChart, Line } from 'recharts';
import { useI18n } from '../../i18n/locale';
import type { EnrichedActivityRow } from './constants';
import { ActivityChartChrome, DualYAxes, dualChartMargin } from './chartPrimitives';
import type { ActivityCategory, SubMetricsState } from './types';

type ActivityCategoryChartProps = {
  category: ActivityCategory;
  subMetrics: SubMetricsState;
  data: EnrichedActivityRow[];
};

export default function ActivityCategoryChart({
  category,
  subMetrics,
  data,
}: ActivityCategoryChartProps) {
  const { t } = useI18n();
  const common = <ActivityChartChrome />;

  switch (category) {
    case 'query': {
      const q = subMetrics.query;
      const needsLeft = q.count;
      const needsRight = q.latency;
      return (
        <ComposedChart data={data} margin={dualChartMargin(needsRight)}>
          {common}
          <DualYAxes needsLeft={needsLeft} needsRight={needsRight} />
          {q.count && (
            <Bar
              yAxisId="left"
              dataKey="query_count"
              name={t('stats.chart.queryCount')}
              fill="#10b981"
              fillOpacity={0.85}
              radius={[2, 2, 0, 0]}
            />
          )}
          {q.latency && (
            <>
              <Area
                yAxisId={needsRight ? 'right' : 'left'}
                dataKey="query_latency_range"
                name={t('stats.chart.latencyBand')}
                fill="#f59e0b"
                fillOpacity={0.22}
                stroke="none"
                activeDot={false}
              />
              <Line
                yAxisId={needsRight ? 'right' : 'left'}
                type="monotone"
                dataKey="query_avg"
                name={t('stats.chart.avgLatency')}
                stroke="#f59e0b"
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 4 }}
              />
            </>
          )}
        </ComposedChart>
      );
    }
    case 'ingestion': {
      const ing = subMetrics.ingestion;
      const needsLeft = ing.count;
      const needsRight = ing.size;
      return (
        <ComposedChart data={data} margin={dualChartMargin(needsRight)}>
          {common}
          <DualYAxes needsLeft={needsLeft} needsRight={needsRight} />
          {ing.count && (
            <Bar
              yAxisId="left"
              dataKey="ingest_count"
              name={t('stats.chart.ingestCount')}
              fill="#3b82f6"
              fillOpacity={0.85}
              radius={[2, 2, 0, 0]}
            />
          )}
          {ing.size && (
            <Line
              yAxisId={needsRight ? 'right' : 'left'}
              type="monotone"
              dataKey="ingest_size_avg"
              name={t('stats.chart.avgBodySize')}
              stroke="#60a5fa"
              strokeWidth={2}
              dot={false}
              activeDot={{ r: 4 }}
            />
          )}
        </ComposedChart>
      );
    }
    case 'insertion': {
      const ins = subMetrics.insertion;
      const needsLeft = ins.count;
      const needsRight = ins.cost;
      return (
        <ComposedChart data={data} margin={dualChartMargin(needsRight)}>
          {common}
          <DualYAxes needsLeft={needsLeft} needsRight={needsRight} />
          {ins.count && (
            <Bar
              yAxisId="left"
              dataKey="insert_total_count"
              name={t('stats.chart.rowsInserted')}
              fill="#14b8a6"
              fillOpacity={0.85}
              radius={[2, 2, 0, 0]}
            />
          )}
          {ins.cost && (
            <Line
              yAxisId={needsRight ? 'right' : 'left'}
              type="monotone"
              dataKey="insert_total_cost"
              name={t('stats.chart.insertCost')}
              stroke="#2dd4bf"
              strokeWidth={2}
              dot={false}
              activeDot={{ r: 4 }}
            />
          )}
        </ComposedChart>
      );
    }
    case 'connections': {
      const conn = subMetrics.connections;
      return (
        <ComposedChart data={data} margin={dualChartMargin(false)}>
          {common}
          <DualYAxes needsLeft needsRight={false} />
          {conn.http && (
            <Line
              yAxisId="left"
              type="monotone"
              dataKey="http_conn_count"
              name={t('stats.chart.httpConn')}
              stroke="#ec4899"
              strokeWidth={2}
              dot={false}
              activeDot={{ r: 4 }}
            />
          )}
          {conn.sse && (
            <Line
              yAxisId="left"
              type="monotone"
              dataKey="sse_session_count"
              name={t('stats.chart.sseSessions')}
              stroke="#8b5cf6"
              strokeWidth={2}
              dot={false}
              activeDot={{ r: 4 }}
            />
          )}
        </ComposedChart>
      );
    }
  }
}
