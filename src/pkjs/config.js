module.exports = [
  { type: 'heading', defaultValue: 'watchface1' },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Display' },
      { type: 'toggle', messageKey: 'DARK_MODE',       label: 'Dark mode',           defaultValue: true  }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Hourly Chime' },
      { type: 'toggle', messageKey: 'CHIME_ENABLED',  label: 'Enable chime',         defaultValue: true  },
      { type: 'toggle', messageKey: 'CHIME_VIBRATE',  label: 'Vibrate on hour',      defaultValue: false },
      { type: 'toggle', messageKey: 'QUIET_ENABLED',  label: 'Quiet hours',          defaultValue: false },
      { type: 'slider', messageKey: 'QUIET_FROM',     label: 'Quiet from (hour)',    defaultValue: 22, min: 0, max: 23, step: 1 },
      { type: 'slider', messageKey: 'QUIET_TO',       label: 'Quiet to (hour)',      defaultValue: 7,  min: 0, max: 23, step: 1 }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Weather' },
      { type: 'toggle', messageKey: 'WEATHER_ON_SHAKE', label: 'Show on wrist shake', defaultValue: false }
    ]
  },
  { type: 'submit', defaultValue: 'Save Settings' }
];
