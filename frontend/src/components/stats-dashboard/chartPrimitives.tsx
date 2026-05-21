import { CartesianGrid, Legend, Tooltip, XAxis, YAxis } from 'recharts';
import {
  CHART_AXIS,
  CHART_GRID,
  chartTooltipStyle,
  formatChartTime,
  formatChartTooltipLabel,
} from './constants';

export function ActivityChartChrome() {
  return (
    <>
      <CartesianGrid strokeDasharray="3 3" stroke={CHART_GRID} />
      <XAxis dataKey="until" tickFormatter={formatChartTime} stroke={CHART_AXIS} fontSize={11} />
      <Tooltip contentStyle={chartTooltipStyle} labelFormatter={formatChartTooltipLabel} />
      <Legend verticalAlign="top" height={36} />
    </>
  );
}

type DualAxisProps = {
  needsLeft: boolean;
  needsRight: boolean;
};

export function DualYAxes({ needsLeft, needsRight }: DualAxisProps) {
  return (
    <>
      {needsLeft && <YAxis yAxisId="left" stroke={CHART_AXIS} fontSize={11} />}
      {needsRight && (
        <YAxis yAxisId="right" orientation="right" stroke={CHART_AXIS} fontSize={11} />
      )}
    </>
  );
}

export function dualChartMargin(needsRight: boolean) {
  return { top: 10, right: needsRight ? 12 : 10, left: -12, bottom: 0 };
}
