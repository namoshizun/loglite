import type { ChartOptions } from 'chart.js';
import type { StatsChartWindow } from './constants';
import { formatChartTimeFromMs, formatChartTooltipLabel } from './constants';

export type ChartColors = {
  grid: string;
  axis: string;
  tooltipBg: string;
  tooltipBorder: string;
  tooltipFg: string;
};

export function readChartColors(_theme?: string): ChartColors {
  void _theme;
  const style = getComputedStyle(document.documentElement);
  const pick = (name: string, fallback: string) => style.getPropertyValue(name).trim() || fallback;

  return {
    grid: pick('--chart-grid', '#27272a'),
    axis: pick('--chart-axis', '#71717a'),
    tooltipBg: pick('--chart-tooltip-bg', '#18181b'),
    tooltipBorder: pick('--chart-tooltip-border', '#27272a'),
    tooltipFg: pick('--chart-tooltip-fg', '#fafafa'),
  };
}

type ScaleOptionsParams = {
  colors: ChartColors;
  window: StatsChartWindow;
  needsRightAxis: boolean;
  showLeftAxis: boolean;
};

function timeXScale(colors: ChartColors, window: StatsChartWindow) {
  const tickColor = colors.axis;
  return {
    type: 'time' as const,
    min: window.since,
    max: window.until,
    ticks: {
      color: tickColor,
      maxRotation: 0,
      autoSkip: true,
      maxTicksLimit: 8,
      callback: (value: string | number) => formatChartTimeFromMs(Number(value)),
    },
    grid: { color: colors.grid },
  };
}

export function activityScaleOptions({
  colors,
  window,
  needsRightAxis,
  showLeftAxis,
}: ScaleOptionsParams): ChartOptions<'bar'>['scales'] {
  const tickColor = colors.axis;
  return {
    x: timeXScale(colors, window),
    y: {
      type: 'linear',
      position: 'left',
      display: showLeftAxis,
      ticks: { color: tickColor, font: { size: 11 } },
      grid: { color: colors.grid },
    },
    y1: {
      type: 'linear',
      position: 'right',
      display: needsRightAxis,
      ticks: { color: tickColor, font: { size: 11 } },
      grid: { drawOnChartArea: false },
    },
  };
}

export function databaseScaleOptions(
  colors: ChartColors,
  window: StatsChartWindow,
): ChartOptions<'line'>['scales'] {
  const tickColor = colors.axis;
  const x = timeXScale(colors, window);
  return {
    x: {
      ...x,
      ticks: {
        ...x.ticks,
        font: { size: 10 },
        maxTicksLimit: 6,
      },
    },
    y: {
      ticks: { color: tickColor, font: { size: 10 } },
      grid: { color: colors.grid },
    },
  };
}

export function statsChartZoomPlugin(colors: ChartColors): ChartOptions['plugins'] {
  return {
    zoom: {
      pan: {
        enabled: true,
        mode: 'x',
        modifierKey: 'shift',
      },
      zoom: {
        wheel: { enabled: true, speed: 0.08 },
        drag: {
          enabled: true,
          backgroundColor: 'rgba(59, 130, 246, 0.12)',
          borderColor: 'rgba(59, 130, 246, 0.55)',
          borderWidth: 1,
        },
        mode: 'x',
      },
      limits: {
        x: { min: 'original', max: 'original' },
      },
    },
    legend: {
      position: 'top',
      labels: { color: colors.axis, boxWidth: 12, font: { size: 11 } },
    },
    tooltip: {
      backgroundColor: colors.tooltipBg,
      borderColor: colors.tooltipBorder,
      borderWidth: 1,
      titleColor: colors.tooltipFg,
      bodyColor: colors.tooltipFg,
      callbacks: {
        title: (items) => {
          const parsedX = items[0]?.parsed?.x;
          if (parsedX != null && typeof parsedX === 'number') {
            return formatChartTooltipLabel(new Date(parsedX).toISOString());
          }
          const label = items[0]?.label;
          return label ? formatChartTooltipLabel(label) : '';
        },
      },
    },
  };
}

export function baseChartPlugins(colors: ChartColors): ChartOptions['plugins'] {
  return statsChartZoomPlugin(colors);
}

export const chartLayout = {
  responsive: true,
  maintainAspectRatio: false,
} as const;
