// Clay configuration for the watchface1 settings UI.
// Shown when the user taps the gear icon in the Pebble phone app.
// messageKey strings must match entries in package.json messageKeys array —
// that array defines the numeric key IDs Clay uses when sending AppMessages.
// When adding a new setting here, add its messageKey to package.json AND
// update the KEY_* constants in index.js, then run scripts/build.sh.
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
      { type: 'slider', messageKey: 'QUIET_TO',       label: 'Quiet to (hour)',      defaultValue: 7,  min: 0, max: 23, step: 1 },
      { type: 'toggle', messageKey: 'CHIME_ON_SHAKE',  label: 'Chime on wrist shake',           defaultValue: false },
      { type: 'toggle', messageKey: 'CHIME_RESPECT_QT', label: 'Respect Pebble Quiet Time',  defaultValue: true  },
      { type: 'slider', messageKey: 'CHIME_VOLUME',     label: 'Chime volume',               defaultValue: 100, min: 0, max: 100, step: 10 }
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
