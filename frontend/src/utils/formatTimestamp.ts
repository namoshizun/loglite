const dateTimeMsOptions: Intl.DateTimeFormatOptions = {
  year: 'numeric',
  month: 'numeric',
  day: 'numeric',
  hour: 'numeric',
  minute: '2-digit',
  second: '2-digit',
  fractionalSecondDigits: 3,
};

const timeMsOptions: Intl.DateTimeFormatOptions = {
  hour: 'numeric',
  minute: '2-digit',
  second: '2-digit',
  fractionalSecondDigits: 3,
};

export function formatDateTimeMs(value: Date | string | number): string {
  return new Date(value).toLocaleString(undefined, dateTimeMsOptions);
}

export function formatTimeMs(value: Date | string | number): string {
  return new Date(value).toLocaleTimeString(undefined, timeMsOptions);
}
